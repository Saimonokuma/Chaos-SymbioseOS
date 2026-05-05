#!/usr/bin/env bash
set -euo pipefail

# Phase 4.1: QEMU Integration Testing
# Crucible: PATTERN-004 (quote all vars), PATTERN-007 (check exit codes)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../../build"
QEMU_IMG="${QEMU_PATH:-qemu-system-x86_64}"
OVMF_CODE="${SCRIPT_DIR}/fixtures/OVMF_CODE.fd"
OVMF_VARS="${SCRIPT_DIR}/fixtures/OVMF_VARS.fd"
TEST_DISK="${SCRIPT_DIR}/fixtures/test_disk.qcow2"

# Crucible: PATTERN-012 (pinned versions)
# shellcheck disable=SC2034
QEMU_VERSION="8.2.0"

echo "========================================"
echo ">> CRUCIBLE QEMU INTEGRATION TEST"
echo "========================================"
echo "QEMU: ${QEMU_IMG}"
echo "Build: ${BUILD_DIR}"
echo ""

# Step 1: Create test disk if missing
if [[ ! -f "${TEST_DISK}" ]]; then
	echo "[1/6] Creating test disk image..."
	"${QEMU_IMG}" create -f qcow2 "${TEST_DISK}" 20G
else
	echo "[1/6] Test disk image exists, skipping creation"
fi

# Step 2: Build Symbiose Bridge driver (cross-compile)
echo "[2/6] Building Symbiose Bridge driver..."
mkdir -p "${BUILD_DIR}"
cmake -S "${SCRIPT_DIR}/../.." -B "${BUILD_DIR}" \
	-DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/../toolchains/wdk-x64.cmake" \
	-DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --target symbiose_bridge

# Step 3: Build ChaosLoader
echo "[3/6] Building ChaosLoader..."
cmake --build "${BUILD_DIR}" --target ChaosLoader

# Step 4: Build IRC Neural Bus
echo "[4/6] Building IRC Neural Bus..."
cmake --build "${BUILD_DIR}" --target symbiose_ircd

# Step 5: Build VFS Manager
echo "[5/6] Building VFS Storage Manager..."
cmake --build "${BUILD_DIR}" --target vfs_manager

# Step 6: Run QEMU with Symbiose Bridge
echo "[6/6] Launching QEMU with Symbiose Bridge..."
"${QEMU_IMG}" \
	-machine q35,accel=kvm \
	-cpu host \
	-m 4096 \
	-smp 4 \
	-drive if=pflash,format=raw,readonly=on,file="${OVMF_CODE}" \
	-drive if=pflash,format=raw,file="${OVMF_VARS}" \
	-drive file="${TEST_DISK}",format=qcow2,if=virtio \
	-drive file="${BUILD_DIR}/symbiose_bridge_test.vhdx",format=vhdx,if=virtio \
	-netdev user,id=net0,hostfwd=tcp::6667-:6667 \
	-device virtio-net-pci,netdev=net0 \
	-serial stdio \
	-monitor unix:/tmp/symbiose_qemu.sock,server,nowait \
	-d int,cpu_reset \
	2>&1 | tee "${SCRIPT_DIR}/test_output.log"

echo ""
echo "========================================"
echo ">> QEMU session ended"
echo ">> Log: ${SCRIPT_DIR}/test_output.log"
echo "========================================"