#!/usr/bin/env bash
set -euo pipefail

# Phase 0.1: Forensic inventory of CHAOS 1.5/
# Crucible Pattern: PATTERN-005 (no hardcoded paths)

SEED_DIR="${1:-./CHAOS 1.5}"
INVENTORY_FILE="./02_Symbiose_Bridge/docs/seed_inventory.md"

mkdir -p "$(dirname "$INVENTORY_FILE")"

echo "# CHAOS 1.5 Seed Inventory" > "$INVENTORY_FILE"
echo "Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$INVENTORY_FILE"
echo "" >> "$INVENTORY_FILE"

# Recursive file listing with hashes
find "$SEED_DIR" -type f -print0 | while IFS= read -r -d '' file; do
	hash=$(sha256sum "$file" | awk '{print $1}')
	size=$(stat --format='%s' "$file" 2>/dev/null || stat -f '%z' "$file")
	ftype=$(file -b "$file")
	echo "- \`$file\` | SHA256: \`$hash\` | Size: ${size}B | Type: $ftype" >> "$INVENTORY_FILE"
done

echo "[✓] Seed inventory written to $INVENTORY_FILE"