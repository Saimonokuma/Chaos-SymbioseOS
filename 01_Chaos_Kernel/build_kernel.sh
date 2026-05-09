#!/bin/bash
# ══════════════════════════════════════════════════════════════════════════
# build_kernel.sh — KERNEL-003: Build x86_64 SymbioseOS guest kernel
#
# Usage:
#   ./build_kernel.sh [KERNEL_SRC_DIR]
#
# Prerequisites:
#   - Linux kernel source tree (e.g., linux-6.x)
#   - Cross-compilation toolchain (gcc, make, bc, flex, bison, libelf-dev)
#   - symbiose_defconfig in this directory
#
# Output:
#   - bin/BZIMAGE (x86_64 boot executable)
#
# Acceptance Criteria (KERNEL-003):
#   file bin/BZIMAGE → "Linux kernel x86 boot executable bzImage"
#   64-bit boot header; build completes without error
#
# Reference: Interactive_Plan.md §XIV·5, §XI KERNEL-003
# ══════════════════════════════════════════════════════════════════════════

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_SRC="${1:-$SCRIPT_DIR/linux}"
DEFCONFIG="$SCRIPT_DIR/symbiose_defconfig"
OUTPUT_DIR="$SCRIPT_DIR/bin"

echo "════════════════════════════════════════════════════════════"
echo "  SymbioseOS x86_64 Kernel Build"
echo "════════════════════════════════════════════════════════════"
echo ""

# ── Step 0: Validate prerequisites ───────────────────────────────────
if [ ! -d "$KERNEL_SRC" ]; then
    echo "[KERNEL-003] ERROR: Kernel source not found at $KERNEL_SRC"
    echo "[KERNEL-003] Usage: $0 /path/to/linux-6.x"
    echo ""
    echo "[KERNEL-003] To get the kernel source:"
    echo "  git clone --depth=1 --branch v6.12 https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git $KERNEL_SRC"
    exit 1
fi

if [ ! -f "$DEFCONFIG" ]; then
    echo "[KERNEL-003] ERROR: symbiose_defconfig not found at $DEFCONFIG"
    exit 1
fi

# ── Step 1: Strip OpenMosix patches first (KERNEL-001) ───────────────
if [ -f "$SCRIPT_DIR/strip_openmosix.sh" ]; then
    echo "[KERNEL-003] Running KERNEL-001: strip_openmosix.sh..."
    bash "$SCRIPT_DIR/strip_openmosix.sh"
    echo ""
fi

# ── Step 2: Install symbiose_defconfig ───────────────────────────────
echo "[KERNEL-003] Installing symbiose_defconfig..."
mkdir -p "$KERNEL_SRC/arch/x86/configs"
cp "$DEFCONFIG" "$KERNEL_SRC/arch/x86/configs/symbiose_defconfig"

# ── Step 3: Configure kernel ─────────────────────────────────────────
echo "[KERNEL-003] Running make symbiose_defconfig..."
make -C "$KERNEL_SRC" ARCH=x86_64 symbiose_defconfig

# ── Step 4: Verify critical configs ──────────────────────────────────
echo "[KERNEL-003] Verifying critical kernel configs..."
CONFIG_FILE="$KERNEL_SRC/.config"

# KERNEL-002: Core mandatory configs
REQUIRED_CONFIGS=(
    "CONFIG_64BIT=y"
    "CONFIG_VIRTIO_PCI=y"
    "CONFIG_VIRTIO_NET=y"
    "CONFIG_BLK_DEV_RAM=y"
    "CONFIG_SERIAL_8250=y"
    "CONFIG_TMPFS=y"
    "CONFIG_CGROUPS=y"
    "CONFIG_CHECKPOINT_RESTORE=y"
    "CONFIG_BPF_SYSCALL=y"
    "CONFIG_INFINIBAND=y"
)

# KERNEL-005: io_uring
REQUIRED_CONFIGS+=("CONFIG_IO_URING=y")

# KERNEL-007: Serial console
REQUIRED_CONFIGS+=("CONFIG_SERIAL_8250_CONSOLE=y")

# KERNEL-008: CRIU namespaces
REQUIRED_CONFIGS+=(
    "CONFIG_NAMESPACES=y"
    "CONFIG_PID_NS=y"
    "CONFIG_NET_NS=y"
    "CONFIG_UTS_NS=y"
    "CONFIG_IPC_NS=y"
)

# KERNEL-009: YeAH TCP + CAKE (11 configs)
REQUIRED_CONFIGS+=(
    "CONFIG_TCP_CONG_YEAH=y"
    "CONFIG_NET_SCH_CAKE=y"
    "CONFIG_NET_SCH_FQ_CODEL=y"
    "CONFIG_NET_CLS_FW=y"
    "CONFIG_NET_CLS_ACT=y"
    "CONFIG_NET_CLS_U32=y"
    "CONFIG_NET_ACT_MIRRED=y"
    "CONFIG_NET_SCH_INGRESS=y"
    "CONFIG_IFB=y"
    "CONFIG_BQL=y"
)

PASS=0
FAIL=0
for cfg in "${REQUIRED_CONFIGS[@]}"; do
    KEY=$(echo "$cfg" | cut -d= -f1)
    if grep -q "^${cfg}$" "$CONFIG_FILE" 2>/dev/null; then
        PASS=$((PASS + 1))
    else
        echo "  ✗ MISSING: $cfg"
        FAIL=$((FAIL + 1))
    fi
done

# Verify CONFIG_MODULES is not set
if grep -q "^CONFIG_MODULES=y" "$CONFIG_FILE" 2>/dev/null; then
    echo "  ✗ CONFIG_MODULES=y FOUND — must be disabled (monolithic)"
    FAIL=$((FAIL + 1))
else
    PASS=$((PASS + 1))
fi

echo "[KERNEL-003] Config check: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    echo "[KERNEL-003] WARNING: Some configs missing — build may not meet acceptance criteria"
fi

# ── Step 5: Build kernel ─────────────────────────────────────────────
NPROC=$(nproc 2>/dev/null || echo 4)
echo ""
echo "[KERNEL-003] Building bzImage with $NPROC threads..."
echo "[KERNEL-003] make ARCH=x86_64 -j$NPROC bzImage"
echo ""

make -C "$KERNEL_SRC" ARCH=x86_64 -j"$NPROC" bzImage

# ── Step 6: Copy output ──────────────────────────────────────────────
mkdir -p "$OUTPUT_DIR"
cp "$KERNEL_SRC/arch/x86/boot/bzImage" "$OUTPUT_DIR/BZIMAGE"

# ── Step 7: Verify output ────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════════════════════════"
if [ -f "$OUTPUT_DIR/BZIMAGE" ]; then
    FILE_INFO=$(file "$OUTPUT_DIR/BZIMAGE")
    SIZE=$(du -h "$OUTPUT_DIR/BZIMAGE" | cut -f1)

    echo "[KERNEL-003] ✓ BUILD SUCCESSFUL"
    echo "[KERNEL-003]   Output: $OUTPUT_DIR/BZIMAGE ($SIZE)"
    echo "[KERNEL-003]   Type: $FILE_INFO"

    if echo "$FILE_INFO" | grep -q "Linux kernel"; then
        echo "[KERNEL-003] ✓ ACCEPTANCE CRITERIA MET"
    else
        echo "[KERNEL-003] ⚠ Warning: file type doesn't match expected 'Linux kernel'"
    fi
else
    echo "[KERNEL-003] ✗ BUILD FAILED — BZIMAGE not found"
    exit 1
fi
echo "════════════════════════════════════════════════════════════"
