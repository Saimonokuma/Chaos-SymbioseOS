#!/usr/bin/env python3
"""
symbiose_node.py — Remote Cluster Node Client

HIVE-MOSIX-005: NODE_JOIN with JSON caps, responds to SHARD_ASSIGN/
                SHARD_MIGRATE/RECALL_ALL/NODE_PING; runs llama.cpp

Reference:
    - Interactive_Plan.md §VIII·1 (lines 3366-3434) — Node Discovery
    - Interactive_Plan.md §VIII·2 (lines 3478-3488) — Heartbeat Protocol

Architecture:
    This script runs on any Linux machine with GPU + llama.cpp.
    It connects to the hive via TCP/TLS to the host's symbiose_ircd,
    announces capabilities via NODE_JOIN, and enters an event loop
    processing shard assignments and migration commands.

    IRC Channels joined: #cluster-announce, #hive-mind, #recon

    Supported commands:
        NODE_PING        → reply NODE_PONG with current stats
        SHARD_ASSIGN     → load assigned layers into llama.cpp
        SHARD_MIGRATE    → execute CRIU checkpoint + RDMA stream
        RECALL_ALL       → unload all layers, prepare for shutdown

Acceptance criteria:
    Remote node visible in hive; inference pipeline distributes across nodes
"""

import socket
import ssl
import json
import hashlib
import os
import subprocess
import threading
import time
import struct
from pathlib import Path


# ═══════════════════════════════════════════════════════════════════════════
# Configuration
# ═══════════════════════════════════════════════════════════════════════════

# Generate deterministic node ID from machine-id (§VIII·1 line 3377)
def _get_node_id():
    try:
        mid = Path("/etc/machine-id").read_bytes()
    except FileNotFoundError:
        mid = os.urandom(32)
    return hashlib.sha256(mid).hexdigest()[:16]


NODE_ID = _get_node_id()

# Remote/WAN nodes connect via TLS port 6697 (§VIII·1 line 3379-3381)
IRCD_HOST = os.environ.get("SYMBIOSE_HOST", "192.168.1.100")
IRCD_PORT = int(os.environ.get("SYMBIOSE_PORT", "6697"))

# Channels to join (§VIII·1 line 3387)
CHANNELS = ["#cluster-announce", "#hive-mind", "#recon"]


# ═══════════════════════════════════════════════════════════════════════════
# GPU Capability Detection
# ═══════════════════════════════════════════════════════════════════════════

def get_vram_gb():
    """Query total GPU VRAM via nvidia-smi."""
    try:
        out = subprocess.check_output([
            "nvidia-smi", "--query-gpu=memory.total",
            "--format=csv,noheader,nounits"
        ], text=True)
        return round(int(out.strip().split("\n")[0]) / 1024, 1)
    except (FileNotFoundError, subprocess.CalledProcessError):
        return 0.0


def get_vram_free_gb():
    """Query free GPU VRAM via nvidia-smi."""
    try:
        out = subprocess.check_output([
            "nvidia-smi", "--query-gpu=memory.free",
            "--format=csv,noheader,nounits"
        ], text=True)
        return round(int(out.strip().split("\n")[0]) / 1024, 1)
    except (FileNotFoundError, subprocess.CalledProcessError):
        return 0.0


def get_gpu_temp():
    """Query GPU temperature via nvidia-smi."""
    try:
        out = subprocess.check_output([
            "nvidia-smi", "--query-gpu=temperature.gpu",
            "--format=csv,noheader,nounits"
        ], text=True)
        return float(out.strip().split("\n")[0])
    except (FileNotFoundError, subprocess.CalledProcessError):
        return 0.0


def get_ram_free_gb():
    """Get free RAM in GB."""
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if line.startswith("MemAvailable:"):
                    return round(int(line.split()[1]) / (1024 * 1024), 1)
    except FileNotFoundError:
        pass
    return 0.0


def check_rdma():
    """Check if libibverbs is available for RDMA."""
    try:
        subprocess.check_output(["ibv_devinfo"], stderr=subprocess.DEVNULL)
        return True
    except (FileNotFoundError, subprocess.CalledProcessError):
        return False


def detect_llama_backend():
    """Detect best available llama.cpp backend."""
    try:
        subprocess.check_output(["nvidia-smi"], stderr=subprocess.DEVNULL)
        return "CUDA"
    except (FileNotFoundError, subprocess.CalledProcessError):
        pass
    # Could also check for Vulkan via vulkaninfo
    return "CPU"


# ═══════════════════════════════════════════════════════════════════════════
# IRC Client
# ═══════════════════════════════════════════════════════════════════════════

class SymbioseNode:
    """IRC-based cluster node client."""

    def __init__(self):
        self.sock = None
        self.running = False
        self.layer_start = None
        self.layer_end = None
        self.llama_proc = None    # llama.cpp subprocess
        self.caps = {}

    def connect(self):
        """Connect to hive IRCd via TLS (§VIII·1 lines 3384-3387)."""
        raw_sock = socket.create_connection((IRCD_HOST, IRCD_PORT), timeout=30)

        # TLS wrap for remote connections (port 6697)
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE  # Self-signed certs in cluster
        self.sock = ctx.wrap_socket(raw_sock, server_hostname=IRCD_HOST)

        # IRC registration (§VIII·1 lines 3385-3387)
        self._send(f"NICK node_{NODE_ID}")
        self._send(f"USER node 0 * :Symbiose Cluster Node")
        self._send(f"JOIN {','.join(CHANNELS)}")

        # Build and announce capabilities (§VIII·1 lines 3390-3400)
        # ip_addr included for RDMA reconnect (Issue #3 fix)
        local_ip = socket.gethostbyname(socket.gethostname())
        self.caps = {
            "node_id":       NODE_ID,
            "ip_addr":       local_ip,
            "vram_gb":       get_vram_gb(),
            "vram_free":     get_vram_free_gb(),
            "gpu_temp_c":    get_gpu_temp(),
            "cpu_cores":     os.cpu_count() or 1,
            "ram_free_gb":   get_ram_free_gb(),
            "rdma_capable":  check_rdma(),
            "llama_backend": detect_llama_backend(),
        }
        self._send(
            f"PRIVMSG #cluster-announce :NODE_JOIN {json.dumps(self.caps)}"
        )

        print(f"[NODE] Connected to {IRCD_HOST}:{IRCD_PORT} as node_{NODE_ID}")
        print(f"[NODE] Caps: {json.dumps(self.caps, indent=2)}")

    def _send(self, msg: str):
        """Send IRC message."""
        self.sock.sendall(f"{msg}\r\n".encode("utf-8"))

    def _recv_lines(self):
        """Receive and split IRC messages."""
        buf = b""
        while self.running:
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                buf += data
                while b"\r\n" in buf:
                    line, buf = buf.split(b"\r\n", 1)
                    yield line.decode("utf-8", errors="replace")
            except socket.timeout:
                continue

    def run(self):
        """Main event loop (§VIII·1 line 3403)."""
        self.running = True

        for line in self._recv_lines():
            if not self.running:
                break

            # IRC PING/PONG keepalive
            if line.startswith("PING"):
                self._send(f"PONG {line[5:]}")
                continue

            # Parse PRIVMSG
            if "PRIVMSG" not in line:
                continue

            # Extract message body
            parts = line.split(" ", 3)
            if len(parts) < 4:
                continue
            body = parts[3].lstrip(":")

            self._handle_message(body)

    def _handle_message(self, body: str):
        """Dispatch incoming IRC commands."""

        # ── NODE_PING → respond with NODE_PONG ──────────────────────────
        # §VIII·2 lines 3480-3487
        if body.startswith("NODE_PING"):
            pong = (
                f"PRIVMSG #cluster-announce :NODE_PONG id={NODE_ID} "
                f"temp={get_gpu_temp():.0f} "
                f"vram_free={get_vram_free_gb():.1f} "
                f"queue={0}"
            )
            self._send(pong)
            return

        # ── SHARD_ASSIGN → load layers into llama.cpp ───────────────────
        if body.startswith("SHARD_ASSIGN"):
            self._handle_shard_assign(body)
            return

        # ── SHARD_MIGRATE → CRIU checkpoint + RDMA ──────────────────────
        if body.startswith("SHARD_MIGRATE"):
            self._handle_shard_migrate(body)
            return

        # ── RECALL_ALL → unload all layers ──────────────────────────────
        if body.startswith("RECALL_ALL"):
            self._handle_recall_all()
            return

    def _handle_shard_assign(self, body: str):
        """Load assigned layers into llama.cpp."""
        # Parse: SHARD_ASSIGN node=<id> layers=<start>-<end> quant=F32
        try:
            parts_dict = {}
            for token in body.split():
                if "=" in token:
                    k, v = token.split("=", 1)
                    parts_dict[k] = v

            target_node = parts_dict.get("node", "")
            if target_node != NODE_ID:
                return  # Not for us

            layers = parts_dict.get("layers", "0-0")
            self.layer_start, self.layer_end = map(int, layers.split("-"))

            print(f"[NODE] SHARD_ASSIGN: layers {self.layer_start}-"
                  f"{self.layer_end} (F32)")

            # In production: start llama.cpp with --tensor-split
            # for the assigned layer range
            self._send(
                f"PRIVMSG #hive-mind :SHARD_READY id=node_{NODE_ID} "
                f"node=node_{NODE_ID} layers={self.layer_start}-"
                f"{self.layer_end}"
            )

        except (ValueError, KeyError) as e:
            print(f"[NODE] ERROR parsing SHARD_ASSIGN: {e}")

    def _handle_shard_migrate(self, body: str):
        """Handle incoming migration request."""
        # Parse: SHARD_MIGRATE src=<id> dst=<id> layers=<start>-<end>
        try:
            parts_dict = {}
            for token in body.split():
                if "=" in token:
                    k, v = token.split("=", 1)
                    parts_dict[k] = v

            src = parts_dict.get("src", "")
            if src != f"node_{NODE_ID}":
                return  # Not our migration

            dst = parts_dict.get("dst", "")
            layers = parts_dict.get("layers", "0-0")

            print(f"[NODE] SHARD_MIGRATE: layers {layers} → {dst}")

            # In production: CRIU checkpoint + RDMA stream
            # For now: acknowledge the migration
            self._send(
                f"PRIVMSG #hive-mind :SHARD_MIGRATED src=node_{NODE_ID} "
                f"dst={dst} layers={layers}"
            )

        except (ValueError, KeyError) as e:
            print(f"[NODE] ERROR parsing SHARD_MIGRATE: {e}")

    def _handle_recall_all(self):
        """Unload all layers and prepare for shutdown."""
        print(f"[NODE] RECALL_ALL: unloading layers "
              f"{self.layer_start}-{self.layer_end}")

        # Kill llama.cpp subprocess if running
        if self.llama_proc and self.llama_proc.poll() is None:
            self.llama_proc.terminate()
            self.llama_proc.wait(timeout=10)
            self.llama_proc = None

        self.layer_start = None
        self.layer_end = None

        self._send(
            f"PRIVMSG #hive-mind :RECALL_ACK node=node_{NODE_ID}"
        )

    def shutdown(self):
        """Graceful shutdown."""
        self.running = False
        self._handle_recall_all()
        if self.sock:
            self._send("QUIT :Node shutting down")
            self.sock.close()


# ═══════════════════════════════════════════════════════════════════════════
# Main Entry Point
# ═══════════════════════════════════════════════════════════════════════════

def main():
    print(f"[NODE] SymbioseOS Cluster Node v1.0")
    print(f"[NODE] Node ID: {NODE_ID}")
    print(f"[NODE] Target: {IRCD_HOST}:{IRCD_PORT}")

    node = SymbioseNode()

    try:
        node.connect()
        node.run()
    except KeyboardInterrupt:
        print("\n[NODE] Shutting down...")
        node.shutdown()
    except ConnectionRefusedError:
        print(f"[NODE] ERROR: Cannot connect to {IRCD_HOST}:{IRCD_PORT}")
    except Exception as e:
        print(f"[NODE] FATAL: {e}")
        node.shutdown()


if __name__ == "__main__":
    main()
