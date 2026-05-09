#!/bin/bash
# rebuild_initrd.sh — Rebuild initramfs from compiled guest binaries
#
# REFERENCE: Interactive_Plan.md §XVII·2
#
# Creates a cpio+gzip initrd.img containing:
#   /sbin/hive_mind      — PID 1 orchestrator (musl static)
#   /usr/bin/symbiose_ircd — Guest-side IRC Neural Bus (musl static)
#   /init                — symlink → /sbin/hive_mind
#   /dev, /proc, /sys, /tmp — required mount points
#
# Usage:
#   ./rebuild_initrd.sh <output_dir>
#
# Expects binaries in build/ directory (created by CI-003 build-linux job).
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
OUTPUT_DIR="${1:-${BUILD_DIR}}"
INITRD_WORK=$(mktemp -d)

echo "=== rebuild_initrd.sh ==="
echo "Build dir:  ${BUILD_DIR}"
echo "Output dir: ${OUTPUT_DIR}"
echo "Work dir:   ${INITRD_WORK}"

# ── Create directory structure ───────────────────────────────────────────────
mkdir -p "${INITRD_WORK}"/{sbin,usr/bin,dev,proc,sys,tmp,etc}

# ── Copy binaries ───────────────────────────────────────────────────────────
if [ ! -f "${BUILD_DIR}/hive_mind" ]; then
    echo "ERROR: ${BUILD_DIR}/hive_mind not found"
    exit 1
fi

cp "${BUILD_DIR}/hive_mind"      "${INITRD_WORK}/sbin/hive_mind"
cp "${BUILD_DIR}/symbiose_ircd"  "${INITRD_WORK}/usr/bin/symbiose_ircd" 2>/dev/null || true

chmod +x "${INITRD_WORK}/sbin/hive_mind"
chmod +x "${INITRD_WORK}/usr/bin/symbiose_ircd" 2>/dev/null || true

# ── Create /init symlink (PID 1 entry point) ────────────────────────────────
ln -sf /sbin/hive_mind "${INITRD_WORK}/init"

# ── Pack as cpio + gzip ─────────────────────────────────────────────────────
echo "Packing initrd.img..."
(cd "${INITRD_WORK}" && find . | cpio -o -H newc 2>/dev/null | gzip -9 > "${OUTPUT_DIR}/initrd.img")

# ── Verify ──────────────────────────────────────────────────────────────────
echo ""
echo "=== initrd.img contents ==="
gzip -dc "${OUTPUT_DIR}/initrd.img" | cpio -t 2>/dev/null | head -20
echo ""
echo "Size: $(du -h "${OUTPUT_DIR}/initrd.img" | cut -f1)"

# ── Cleanup ─────────────────────────────────────────────────────────────────
rm -rf "${INITRD_WORK}"
echo "=== Done ==="
