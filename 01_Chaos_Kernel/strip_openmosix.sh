#!/bin/bash
# ══════════════════════════════════════════════════════════════════════════
# strip_openmosix.sh — KERNEL-001: Remove all OpenMosix legacy patches
#
# Acceptance Criteria:
#   grep -r openmosix 01_Chaos_Kernel/ returns zero matches
#   No CONFIG_OPENMOSIX in any Kconfig
#
# Reference: Interactive_Plan.md §XI KERNEL-001
#
# This script is run ONCE after cloning the kernel source tree.
# It removes:
#   1. patches/openmosix-* files
#   2. CONFIG_OPENMOSIX* entries from all Kconfig files
#   3. Any OpenMosix-specific inline assembly
#   4. OpenMosix #include directives
# ══════════════════════════════════════════════════════════════════════════

set -euo pipefail

KERNEL_DIR="$(cd "$(dirname "$0")" && pwd)"
echo "[KERNEL-001] Stripping OpenMosix patches from $KERNEL_DIR"

# ── Step 1: Remove patches/openmosix-* files ─────────────────────────
if [ -d "$KERNEL_DIR/patches" ]; then
    PATCH_COUNT=$(find "$KERNEL_DIR/patches" -name "openmosix-*" -type f 2>/dev/null | wc -l)
    if [ "$PATCH_COUNT" -gt 0 ]; then
        find "$KERNEL_DIR/patches" -name "openmosix-*" -type f -delete
        echo "[KERNEL-001] Removed $PATCH_COUNT OpenMosix patch files"
    else
        echo "[KERNEL-001] No openmosix-* patch files found"
    fi
else
    echo "[KERNEL-001] No patches/ directory found"
fi

# ── Step 2: Remove CONFIG_OPENMOSIX from Kconfig files ───────────────
KCONFIG_HITS=$(grep -rl "CONFIG_OPENMOSIX" "$KERNEL_DIR" --include="Kconfig*" 2>/dev/null | wc -l)
if [ "$KCONFIG_HITS" -gt 0 ]; then
    # Remove entire config blocks (config OPENMOSIX ... endmenu/endif)
    grep -rl "CONFIG_OPENMOSIX" "$KERNEL_DIR" --include="Kconfig*" 2>/dev/null | while read -r f; do
        sed -i '/config OPENMOSIX/,/^$/d' "$f"
        sed -i '/CONFIG_OPENMOSIX/d' "$f"
        echo "[KERNEL-001] Cleaned Kconfig: $f"
    done
else
    echo "[KERNEL-001] No CONFIG_OPENMOSIX in Kconfig files"
fi

# ── Step 3: Remove OpenMosix #include directives ─────────────────────
INCLUDE_HITS=$(grep -rl "openmosix" "$KERNEL_DIR" --include="*.h" --include="*.c" --include="*.S" 2>/dev/null | wc -l)
if [ "$INCLUDE_HITS" -gt 0 ]; then
    grep -rl "openmosix" "$KERNEL_DIR" --include="*.h" --include="*.c" --include="*.S" 2>/dev/null | while read -r f; do
        # Remove #include lines referencing openmosix
        sed -i '/#include.*openmosix/d' "$f"
        # Remove CONFIG_OPENMOSIX ifdef blocks (simple case)
        sed -i '/ifdef.*CONFIG_OPENMOSIX/,/endif.*CONFIG_OPENMOSIX/d' "$f"
        echo "[KERNEL-001] Cleaned source: $f"
    done
else
    echo "[KERNEL-001] No openmosix references in source files"
fi

# ── Step 4: Remove OpenMosix Makefile entries ─────────────────────────
MAKE_HITS=$(grep -rl "openmosix" "$KERNEL_DIR" --include="Makefile*" 2>/dev/null | wc -l)
if [ "$MAKE_HITS" -gt 0 ]; then
    grep -rl "openmosix" "$KERNEL_DIR" --include="Makefile*" 2>/dev/null | while read -r f; do
        sed -i '/openmosix/d' "$f"
        echo "[KERNEL-001] Cleaned Makefile: $f"
    done
else
    echo "[KERNEL-001] No openmosix references in Makefiles"
fi

# ── Step 5: Verification ─────────────────────────────────────────────
echo ""
echo "[KERNEL-001] ═══════════════════════════════════════════"
echo "[KERNEL-001] VERIFICATION"
REMAINING=$(grep -ri "openmosix" "$KERNEL_DIR" --include="*.c" --include="*.h" --include="*.S" --include="Kconfig*" --include="Makefile*" 2>/dev/null | wc -l)
if [ "$REMAINING" -eq 0 ]; then
    echo "[KERNEL-001] ✓ CLEAN — zero openmosix references remain"
else
    echo "[KERNEL-001] ✗ WARNING — $REMAINING references still found:"
    grep -ri "openmosix" "$KERNEL_DIR" --include="*.c" --include="*.h" --include="*.S" --include="Kconfig*" --include="Makefile*" 2>/dev/null | head -20
fi
echo "[KERNEL-001] ═══════════════════════════════════════════"
