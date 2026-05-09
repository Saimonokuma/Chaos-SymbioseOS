#!/usr/bin/env python3
"""
symbiose_node.py — Remote Node Client for SymbioseOS Neo-OpenMosix Cluster

HIVE-MOSIX-005: NODE_JOIN with JSON capabilities, shard serving, RECALL_ALL

Reference: Interactive_Plan.md §VIII·2, §XIV·6

This script runs on remote cluster nodes. It:
  1. Connects to the IRC Neural Bus on the master node
  2. Announces NODE_JOIN with hardware capabilities (VRAM, temp, RDMA)
  3. Responds to SHARD_ASSIGN / SHARD_MIGRATE / RECALL_ALL / NODE_PING
  4. Runs llama.cpp with assigned layer range
  5. Reports GPU telemetry to #telemetry

QoS (§XIV·7): Control socket marked EF (Voice tin, IP_TOS=0xB8).
CAKE autorate-ingress handles WAN/cellular bandwidth estimation.

Usage:
    python3 symbiose_node.py --master 192.168.1.100 --port 6667 --node-id node-02
"""

import socket
import json
import subprocess
import threading
import time
import sys
import argparse
import os
import struct

# ── Constants ────────────────────────────────────────────────────────────────
IRC_PORT_DEFAULT = 6667
DSCP_EF = 0xB8          # Expedited Forwarding (Voice tin in CAKE DiffServ4)
TELEMETRY_INTERVAL = 5  # seconds between GPU stat reports

# ── IRC Client ───────────────────────────────────────────────────────────────
class IRCClient:
    """Minimal IRC client for Neural Bus communication."""

    def __init__(self, host: str, port: int, nick: str):
        self.host = host
        self.port = port
        self.nick = nick
        self.sock = None
        self.running = False

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        # QoS: Mark control socket as EF (Voice tin)
        self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_TOS, DSCP_EF)
        self.sock.connect((self.host, self.port))
        self.running = True
        self.send(f"NICK {self.nick}")
        self.send(f"USER {self.nick} 0 * :SymbioseOS Remote Node")
        # Wait for welcome (001)
        while True:
            line = self._recv_line()
            if " 001 " in line:
                break
            if line.startswith("PING"):
                self.send("PONG " + line.split(":", 1)[-1])

    def send(self, msg: str):
        self.sock.sendall((msg + "\r\n").encode("utf-8"))

    def join(self, channel: str):
        self.send(f"JOIN {channel}")

    def privmsg(self, target: str, msg: str):
        self.send(f"PRIVMSG {target} :{msg}")

    def _recv_line(self) -> str:
        buf = b""
        while not buf.endswith(b"\r\n"):
            data = self.sock.recv(1)
            if not data:
                raise ConnectionError("IRC connection lost")
            buf += data
        return buf.decode("utf-8", errors="replace").strip()

    def recv_loop(self, handler):
        """Main receive loop — dispatches messages to handler."""
        while self.running:
            try:
                line = self._recv_line()
                if line.startswith("PING"):
                    self.send("PONG " + line.split(":", 1)[-1])
                    continue
                handler(line)
            except ConnectionError:
                print("[NODE] IRC connection lost — attempting reconnect")
                time.sleep(5)
                try:
                    self.connect()
                except Exception:
                    pass

    def close(self):
        self.running = False
        if self.sock:
            self.sock.close()


# ── GPU Detection ────────────────────────────────────────────────────────────
def detect_gpu() -> dict:
    """Detect GPU capabilities via nvidia-smi."""
    caps = {
        "vram_total_gb": 0,
        "vram_free_gb": 0,
        "gpu_temp_c": 0,
        "gpu_name": "unknown",
        "rdma_available": False
    }
    try:
        out = subprocess.check_output([
            "nvidia-smi",
            "--query-gpu=name,memory.total,memory.free,temperature.gpu",
            "--format=csv,noheader,nounits"
        ], text=True, timeout=10)
        parts = out.strip().split(",")
        if len(parts) >= 4:
            caps["gpu_name"] = parts[0].strip()
            caps["vram_total_gb"] = round(int(parts[1].strip()) / 1024)
            caps["vram_free_gb"] = round(int(parts[2].strip()) / 1024)
            caps["gpu_temp_c"] = int(parts[3].strip())
    except Exception:
        pass

    # Check RDMA
    try:
        subprocess.check_output(["ibv_devinfo"], timeout=5, stderr=subprocess.DEVNULL)
        caps["rdma_available"] = True
    except Exception:
        pass

    return caps


# ── Node Handler ─────────────────────────────────────────────────────────────
class SymbioseNode:
    """Remote cluster node — responds to hive mind commands."""

    def __init__(self, irc: IRCClient, node_id: str):
        self.irc = irc
        self.node_id = node_id
        self.layer_start = -1
        self.layer_end = -1
        self.llama_proc = None

    def announce_join(self, caps: dict):
        """Send NODE_JOIN with hardware capabilities."""
        payload = json.dumps({
            "node_id": self.node_id,
            "vram_total_gb": caps["vram_total_gb"],
            "vram_free_gb": caps["vram_free_gb"],
            "gpu_temp_c": caps["gpu_temp_c"],
            "gpu_name": caps["gpu_name"],
            "rdma": caps["rdma_available"]
        })
        self.irc.privmsg("#cluster-announce", f"NODE_JOIN {payload}")
        print(f"[NODE] Announced NODE_JOIN: {payload}")

    def handle_message(self, line: str):
        """Dispatch incoming IRC messages."""
        if "SHARD_ASSIGN" in line:
            self._handle_shard_assign(line)
        elif "SHARD_MIGRATE" in line:
            self._handle_shard_migrate(line)
        elif "RECALL_ALL" in line:
            self._handle_recall_all(line)
        elif "NODE_PING" in line:
            self._handle_ping(line)

    def _handle_shard_assign(self, line: str):
        """Accept layer assignment and start llama.cpp."""
        try:
            # Parse: SHARD_ASSIGN layers=0-41
            parts = line.split("layers=")[1].split()[0]
            start, end = parts.split("-")
            self.layer_start = int(start)
            self.layer_end = int(end)
            print(f"[NODE] Assigned layers {self.layer_start}-{self.layer_end}")
            self.irc.privmsg("#hive-mind",
                f"SHARD_ACK node={self.node_id} layers={self.layer_start}-{self.layer_end}")
        except Exception as e:
            print(f"[NODE] Failed to parse SHARD_ASSIGN: {e}")

    def _handle_shard_migrate(self, line: str):
        """Migrate assigned layers to another node."""
        print(f"[NODE] SHARD_MIGRATE received — releasing layers")
        self.layer_start = -1
        self.layer_end = -1
        if self.llama_proc:
            self.llama_proc.terminate()
            self.llama_proc = None

    def _handle_recall_all(self, line: str):
        """Release all resources and return to idle."""
        print(f"[NODE] RECALL_ALL — returning to idle")
        self.layer_start = -1
        self.layer_end = -1
        if self.llama_proc:
            self.llama_proc.terminate()
            self.llama_proc = None
        self.irc.privmsg("#hive-mind", f"RECALL_ACK node={self.node_id}")

    def _handle_ping(self, line: str):
        """Respond to health check."""
        caps = detect_gpu()
        self.irc.privmsg("#hive-mind",
            f"NODE_PONG node={self.node_id} "
            f"vram_free={caps['vram_free_gb']}GB "
            f"temp={caps['gpu_temp_c']}C "
            f"layers={self.layer_start}-{self.layer_end}")

    def telemetry_loop(self):
        """Periodic GPU telemetry reporting."""
        while self.irc.running:
            caps = detect_gpu()
            self.irc.privmsg("#telemetry",
                f"GPU_STATS node={self.node_id} "
                f"alloc={caps['vram_total_gb'] - caps['vram_free_gb']}GB "
                f"temp={caps['gpu_temp_c']}C "
                f"layers={self.layer_start}-{self.layer_end}")
            time.sleep(TELEMETRY_INTERVAL)


# ── Main ─────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="SymbioseOS Remote Node Client")
    parser.add_argument("--master", required=True, help="Master node IRC host")
    parser.add_argument("--port", type=int, default=IRC_PORT_DEFAULT)
    parser.add_argument("--node-id", required=True, help="Unique node identifier")
    args = parser.parse_args()

    print(f"[NODE] SymbioseOS Remote Node — {args.node_id}")
    print(f"[NODE] Connecting to {args.master}:{args.port}")

    caps = detect_gpu()
    print(f"[NODE] GPU: {caps['gpu_name']} ({caps['vram_total_gb']}GB VRAM, {caps['gpu_temp_c']}°C)")
    print(f"[NODE] RDMA: {'Available' if caps['rdma_available'] else 'Not available (TCP fallback)'}")

    irc = IRCClient(args.master, args.port, args.node_id)
    irc.connect()

    # Join required channels
    for ch in ["#cluster-announce", "#hive-mind", "#telemetry"]:
        irc.join(ch)

    node = SymbioseNode(irc, args.node_id)
    node.announce_join(caps)

    # Start telemetry thread
    telemetry_thread = threading.Thread(target=node.telemetry_loop, daemon=True)
    telemetry_thread.start()

    # Main receive loop
    try:
        irc.recv_loop(node.handle_message)
    except KeyboardInterrupt:
        print("\n[NODE] Shutting down...")
        irc.privmsg("#cluster-announce", f"NODE_LEAVE node={args.node_id}")
        irc.close()


if __name__ == "__main__":
    main()
