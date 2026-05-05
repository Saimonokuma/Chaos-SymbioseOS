#!/usr/bin/env bash
set -euo pipefail

# Phase 0.2: Validate transplantable kernel and ramdisk
# Crucible: PATTERN-008 (TOCTOU-safe file operations)

SEED_DIR="${1:-./CHAOS 1.5}"
BZIMAGE="${SEED_DIR}/CHAOS/BZIMAGE"
CHAOS_RDZ="${SEED_DIR}/CHAOS/CHAOS.RDZ"
VALIDATION_FILE="./02_Symbiose_Bridge/docs/transplant_validation.md"

mkdir -p "$(dirname "$VALIDATION_FILE")"

validate_kernel() {
	local kernel="$1"
	echo "## BZIMAGE Validation" >> "$VALIDATION_FILE"

	if [[ ! -f "$kernel" ]]; then
		echo "❌ FAIL: BZIMAGE not found at $kernel" >> "$VALIDATION_FILE"
		return 1
	fi

	# Check if it's a valid Linux kernel image
	local magic
	magic=$(xxd -l 4 -p "$kernel")

	# Linux kernel images start with specific magic bytes
	# 0xE8 or 0xEB for uncompressed, or gzip header 1F 8B
	echo "- Magic bytes: \`$magic\`" >> "$VALIDATION_FILE"
	echo "- Size: $(stat --format='%s' "$kernel" 2>/dev/null || stat -f '%z' "$kernel") bytes" >> "$VALIDATION_FILE"

	# Check for gzip compression (most kernels are gzip-compressed)
	if [[ "$magic" == "1f8b"* ]]; then
		echo "- Format: gzip-compressed kernel" >> "$VALIDATION_FILE"
	else
		echo "- Format: uncompressed or custom format (requires manual inspection)" >> "$VALIDATION_FILE"
	fi

	echo "✅ BZIMAGE located and catalogued" >> "$VALIDATION_FILE"
}

validate_ramdisk() {
	local rdz="$1"
	echo "## CHAOS.RDZ Validation" >> "$VALIDATION_FILE"

	if [[ ! -f "$rdz" ]]; then
		echo "❌ FAIL: CHAOS.RDZ not found at $rdz" >> "$VALIDATION_FILE"
		return 1
	fi

	local magic
	magic=$(xxd -l 4 -p "$rdz")

	echo "- Magic bytes: \`$magic\`" >> "$VALIDATION_FILE"
	echo "- Size: $(stat --format='%s' "$rdz" 2>/dev/null || stat -f '%z' "$rdz") bytes" >> "$VALIDATION_FILE"

	# Common ramdisk formats: gzip (1f8b), cpio (070701), ext2 (ef53)
	case "$magic" in
		1f8b*) echo "- Format: gzip-compressed" >> "$VALIDATION_FILE" ;;
		3037*) echo "- Format: ASCII cpio archive" >> "$VALIDATION_FILE" ;;
		*) echo "- Format: unknown (requires manual inspection)" >> "$VALIDATION_FILE" ;;
	esac

	echo "✅ CHAOS.RDZ located and catalogued" >> "$VALIDATION_FILE"
}

validate_kernel "$BZIMAGE"
validate_ramdisk "$CHAOS_RDZ"
