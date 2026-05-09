#!/bin/bash
# ══════════════════════════════════════════════════════════════════════════
# verify_kernel_config.sh — Acceptance test for all 9 KERNEL tasks
#
# Validates the built .config against every KERNEL-* acceptance criteria.
# Run after build_kernel.sh to confirm compliance.
#
# Usage: ./verify_kernel_config.sh [path/to/.config]
# ══════════════════════════════════════════════════════════════════════════

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG_FILE="${1:-$SCRIPT_DIR/linux/.config}"

if [ ! -f "$CONFIG_FILE" ]; then
    echo "[VERIFY] ERROR: .config not found at $CONFIG_FILE"
    echo "[VERIFY] Run build_kernel.sh first, or pass path: $0 /path/to/.config"
    exit 1
fi

echo "════════════════════════════════════════════════════════════"
echo "  SymbioseOS Kernel Config Verification"
echo "  Config: $CONFIG_FILE"
echo "════════════════════════════════════════════════════════════"
echo ""

TOTAL=0
PASS=0
FAIL=0

check_config() {
    local task_id="$1"
    local config_key="$2"
    local expected="$3"
    local description="$4"

    TOTAL=$((TOTAL + 1))

    if [ "$expected" = "not set" ]; then
        if grep -q "^# ${config_key} is not set" "$CONFIG_FILE" 2>/dev/null || \
           ! grep -q "^${config_key}=" "$CONFIG_FILE" 2>/dev/null; then
            echo "  ✓ [$task_id] $config_key is not set — $description"
            PASS=$((PASS + 1))
        else
            echo "  ✗ [$task_id] $config_key should NOT be set — $description"
            FAIL=$((FAIL + 1))
        fi
    else
        if grep -q "^${config_key}=${expected}" "$CONFIG_FILE" 2>/dev/null; then
            echo "  ✓ [$task_id] ${config_key}=${expected} — $description"
            PASS=$((PASS + 1))
        else
            echo "  ✗ [$task_id] MISSING ${config_key}=${expected} — $description"
            FAIL=$((FAIL + 1))
        fi
    fi
}

# ── KERNEL-001: OpenMosix stripped ────────────────────────────────────
echo "── KERNEL-001: OpenMosix Stripped ──"
TOTAL=$((TOTAL + 1))
OPENMOSIX_REFS=$(grep -c "openmosix" "$CONFIG_FILE" 2>/dev/null || true)
if [ "$OPENMOSIX_REFS" -eq 0 ]; then
    echo "  ✓ [KERNEL-001] Zero openmosix references in .config"
    PASS=$((PASS + 1))
else
    echo "  ✗ [KERNEL-001] $OPENMOSIX_REFS openmosix references found!"
    FAIL=$((FAIL + 1))
fi
echo ""

# ── KERNEL-002: Core mandatory configs ───────────────────────────────
echo "── KERNEL-002: Core x86_64 Defconfig ──"
check_config "KERNEL-002" "CONFIG_64BIT"            "y"       "64-bit architecture"
check_config "KERNEL-002" "CONFIG_VIRTIO_PCI"       "y"       "VirtIO PCI transport"
check_config "KERNEL-002" "CONFIG_VIRTIO_NET"       "y"       "VirtIO network"
check_config "KERNEL-002" "CONFIG_BLK_DEV_RAM"      "y"       "RAM disk (initrd)"
check_config "KERNEL-002" "CONFIG_SERIAL_8250"      "y"       "Serial 8250 UART"
check_config "KERNEL-002" "CONFIG_TMPFS"            "y"       "tmpfs filesystem"
check_config "KERNEL-002" "CONFIG_CGROUPS"          "y"       "Control groups"
check_config "KERNEL-002" "CONFIG_CHECKPOINT_RESTORE" "y"     "Checkpoint/restore"
check_config "KERNEL-002" "CONFIG_BPF_SYSCALL"      "y"       "BPF syscall"
check_config "KERNEL-002" "CONFIG_INFINIBAND"       "y"       "InfiniBand core"
check_config "KERNEL-002" "CONFIG_MODULES"          "not set" "Monolithic (no modules)"
echo ""

# ── KERNEL-004: Huge Pages Boot Config ───────────────────────────────
echo "── KERNEL-004: Huge Pages ──"
check_config "KERNEL-004" "CONFIG_CMDLINE_BOOL"  "y"  "Custom command line enabled"
check_config "KERNEL-004" "CONFIG_HUGETLBFS"     "y"  "Huge page filesystem"
check_config "KERNEL-004" "CONFIG_HUGETLB_PAGE"  "y"  "Huge page support"
TOTAL=$((TOTAL + 1))
if grep -q "hugepagesz=1G" "$CONFIG_FILE" 2>/dev/null; then
    echo "  ✓ [KERNEL-004] CONFIG_CMDLINE contains hugepagesz=1G"
    PASS=$((PASS + 1))
else
    echo "  ✗ [KERNEL-004] CONFIG_CMDLINE missing hugepagesz=1G"
    FAIL=$((FAIL + 1))
fi
echo ""

# ── KERNEL-005: io_uring ─────────────────────────────────────────────
echo "── KERNEL-005: io_uring ──"
check_config "KERNEL-005" "CONFIG_IO_URING"  "y"  "io_uring async I/O"
echo ""

# ── KERNEL-006: RDMA / InfiniBand ────────────────────────────────────
echo "── KERNEL-006: RDMA/InfiniBand ──"
check_config "KERNEL-006" "CONFIG_INFINIBAND"            "y"  "InfiniBand core"
check_config "KERNEL-006" "CONFIG_INFINIBAND_USER_MAD"   "y"  "User MAD"
check_config "KERNEL-006" "CONFIG_INFINIBAND_USER_ACCESS" "y" "User access verbs"
check_config "KERNEL-006" "CONFIG_MLX5_INFINIBAND"       "y"  "Mellanox ConnectX"
echo ""

# ── KERNEL-007: Serial Console ───────────────────────────────────────
echo "── KERNEL-007: Serial Console ──"
check_config "KERNEL-007" "CONFIG_SERIAL_8250_CONSOLE"  "y"  "Serial 8250 console"
TOTAL=$((TOTAL + 1))
if grep -q "console=ttyS0" "$CONFIG_FILE" 2>/dev/null; then
    echo "  ✓ [KERNEL-007] CONFIG_CMDLINE contains console=ttyS0"
    PASS=$((PASS + 1))
else
    echo "  ✗ [KERNEL-007] CONFIG_CMDLINE missing console=ttyS0"
    FAIL=$((FAIL + 1))
fi
echo ""

# ── KERNEL-008: CRIU Dependencies ────────────────────────────────────
echo "── KERNEL-008: CRIU Namespaces ──"
check_config "KERNEL-008" "CONFIG_CHECKPOINT_RESTORE"  "y"  "Checkpoint/restore"
check_config "KERNEL-008" "CONFIG_NAMESPACES"          "y"  "Namespaces"
check_config "KERNEL-008" "CONFIG_PID_NS"              "y"  "PID namespace"
check_config "KERNEL-008" "CONFIG_NET_NS"              "y"  "Net namespace"
check_config "KERNEL-008" "CONFIG_UTS_NS"              "y"  "UTS namespace"
check_config "KERNEL-008" "CONFIG_IPC_NS"              "y"  "IPC namespace"
echo ""

# ── KERNEL-009: YeAH! TCP + CAKE QoS (11 configs) ───────────────────
echo "── KERNEL-009: YeAH! TCP + CAKE QoS (11 configs) ──"
check_config "KERNEL-009" "CONFIG_TCP_CONG_YEAH"      "y"       "YeAH! TCP"
check_config "KERNEL-009" "CONFIG_NET_SCH_CAKE"       "y"       "CAKE qdisc"
check_config "KERNEL-009" "CONFIG_NET_SCH_FQ_CODEL"   "y"       "FQ-CoDel fallback"
check_config "KERNEL-009" "CONFIG_NET_CLS_FW"         "y"       "fwmark classifier"
check_config "KERNEL-009" "CONFIG_NET_CLS_ACT"        "y"       "tc action subsystem"
check_config "KERNEL-009" "CONFIG_NET_CLS_U32"        "y"       "u32 classifier"
check_config "KERNEL-009" "CONFIG_NET_ACT_MIRRED"     "y"       "mirred redirect"
check_config "KERNEL-009" "CONFIG_NET_SCH_INGRESS"    "y"       "ingress qdisc"
check_config "KERNEL-009" "CONFIG_IFB"                "y"       "IFB device"
check_config "KERNEL-009" "CONFIG_BQL"                "y"       "Byte Queue Limits"

TOTAL=$((TOTAL + 1))
if grep -q 'CONFIG_DEFAULT_TCP_CONG="yeah"' "$CONFIG_FILE" 2>/dev/null; then
    echo "  ✓ [KERNEL-009] DEFAULT_TCP_CONG=\"yeah\""
    PASS=$((PASS + 1))
else
    echo "  ✗ [KERNEL-009] DEFAULT_TCP_CONG should be \"yeah\""
    FAIL=$((FAIL + 1))
fi
echo ""

# ── Summary ──────────────────────────────────────────────────────────
echo "════════════════════════════════════════════════════════════"
echo "  RESULTS: $PASS/$TOTAL passed, $FAIL failed"
echo "════════════════════════════════════════════════════════════"

if [ "$FAIL" -eq 0 ]; then
    echo "  ✅ ALL 9 KERNEL TASKS PASS"
    exit 0
else
    echo "  ❌ $FAIL CHECKS FAILED"
    exit 1
fi
