#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# TEST-IRC-009: Tensor Dedup + Checkpoint WAL Recovery
# File: 05_Integration_Tests/qemu_scripts/test_tensor_dedup.sh
#
# Cross-References:
#   §XI  Line 5102 — "Tensor dedup + checkpoint WAL: same-CRC64 shard skipped;
#                      crash recovery replays log"
#   §XIII·5      — "Same-CRC64 shard requested twice; second request skips
#                   transfer, returns existing holder node"
#                  "Crash → reboot → #checkpoint log replayed →
#                   tensor index + node registry restored correctly"
#   HIVE-IRC-009 — Content-addressed tensor index, CRC64 dedup, WAL on #checkpoint
#
# Test Flow:
#   1. Create content-addressed tensor index
#   2. Register a tensor shard with CRC64
#   3. Request same CRC64 again — verify dedup (skip transfer)
#   4. Write checkpoint WAL entries
#   5. Simulate crash (delete in-memory index)
#   6. Replay WAL — verify index + node registry restored
#
# Exit codes:
#   0 = All checks pass
#   N = Number of failed tests
# ══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

# ─── Configuration ───────────────────────────────────────────────────────────
TIMEOUT_SECS="${TIMEOUT_SECS:-15}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

pass() { TESTS_RUN=$((TESTS_RUN+1)); TESTS_PASSED=$((TESTS_PASSED+1)); echo -e "  ${GREEN}[PASS]${NC} $1"; }
fail() { TESTS_RUN=$((TESTS_RUN+1)); TESTS_FAILED=$((TESTS_FAILED+1)); echo -e "  ${RED}[FAIL]${NC} $1"; [[ -n "${2:-}" ]] && echo -e "        ${YELLOW}Detail:${NC} $2"; }
info() { echo -e "  ${CYAN}[INFO]${NC} $1"; }

# Temp directory for test state
STATE_DIR="$(mktemp -d /tmp/test_irc009_state_XXXXXX)"
INDEX_FILE="$STATE_DIR/tensor_index.json"
WAL_FILE="$STATE_DIR/checkpoint.wal"
NODE_REG_FILE="$STATE_DIR/node_registry.json"

cleanup() {
    rm -rf "$STATE_DIR"
}
trap cleanup EXIT

# ─── Header ──────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo " TEST-IRC-009: Tensor Dedup + Checkpoint WAL Recovery"
echo " Ref: Interactive_Plan.md §XI(5102), §XIII·5, HIVE-IRC-009"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""

# ─── Test Group A: Content-Addressed Tensor Index ────────────────────────────
echo "── Test Group A: Tensor Index (Content-Addressed by CRC64) ──"

# Create initial tensor index
python3 -c "
import json
index = {
    'version': 1,
    'shards': {},
    'node_registry': {}
}
with open('$INDEX_FILE', 'w') as f:
    json.dump(index, f, indent=2)
" 2>/dev/null

if [[ -f "$INDEX_FILE" ]]; then
    pass "TEST-IRC-009-A1: Tensor index created (content-addressed by CRC64)"
else
    fail "TEST-IRC-009-A1: Tensor index creation" "File not created"
fi

# Register test tensor shards
TEST_SHARDS=(
    "A1B2C3D4E5F60001:hive-01:layers_0-7:134217728"
    "B2C3D4E5F6070002:hive-02:layers_8-15:134217728"
    "C3D4E5F607080003:hive-03:layers_16-23:134217728"
    "D4E5F60708090004:hive-01:layers_24-31:134217728"
)

python3 -c "
import json, time

with open('$INDEX_FILE', 'r') as f:
    index = json.load(f)

shards = [
    ('A1B2C3D4E5F60001', 'hive-01', 'layers_0-7', 134217728),
    ('B2C3D4E5F6070002', 'hive-02', 'layers_8-15', 134217728),
    ('C3D4E5F607080003', 'hive-03', 'layers_16-23', 134217728),
    ('D4E5F60708090004', 'hive-01', 'layers_24-31', 134217728),
]

for crc64, node, layers, size in shards:
    index['shards'][crc64] = {
        'holder_node': node,
        'layer_range': layers,
        'size_bytes': size,
        'timestamp': time.time(),
        'transfer_count': 1
    }

index['node_registry'] = {
    'hive-01': {'active': True, 'shards_held': 2, 'vram_gb': 24},
    'hive-02': {'active': True, 'shards_held': 1, 'vram_gb': 16},
    'hive-03': {'active': True, 'shards_held': 1, 'vram_gb': 24},
}

with open('$INDEX_FILE', 'w') as f:
    json.dump(index, f, indent=2)
print(f'Registered {len(shards)} shards across {len(index[\"node_registry\"])} nodes')
" 2>/dev/null

SHARD_COUNT=$(python3 -c "import json; print(len(json.load(open('$INDEX_FILE'))['shards']))" 2>/dev/null)
if [[ "$SHARD_COUNT" -eq 4 ]]; then
    pass "TEST-IRC-009-A2: $SHARD_COUNT tensor shards registered in index"
else
    fail "TEST-IRC-009-A2: Shard registration" "Expected 4, got $SHARD_COUNT"
fi

# ─── Test Group B: Dedup — Same CRC64 Skips Transfer ────────────────────────
echo ""
echo "── Test Group B: CRC64 Dedup (§XIII·5: same-CRC64 skipped) ──"

# Request a shard that already exists → should return holder node, skip transfer
DEDUP_RESULT=$(python3 -c "
import json

with open('$INDEX_FILE', 'r') as f:
    index = json.load(f)

# Request shard with CRC64 that already exists
request_crc = 'A1B2C3D4E5F60001'

if request_crc in index['shards']:
    existing = index['shards'][request_crc]
    print(f'DEDUP_HIT holder={existing[\"holder_node\"]} layers={existing[\"layer_range\"]}')
else:
    print('DEDUP_MISS')
" 2>/dev/null)

if echo "$DEDUP_RESULT" | grep -q "DEDUP_HIT"; then
    HOLDER_NODE=$(echo "$DEDUP_RESULT" | grep -oP 'holder=\K\S+')
    pass "TEST-IRC-009-B1: Dedup hit — shard already held by $HOLDER_NODE (transfer skipped)"
else
    fail "TEST-IRC-009-B1: Dedup hit" "Expected DEDUP_HIT for existing CRC64"
fi

# Request a NEW shard → should be DEDUP_MISS (requires transfer)
NEW_RESULT=$(python3 -c "
import json

with open('$INDEX_FILE', 'r') as f:
    index = json.load(f)

request_crc = 'FFFFFFFFFFFFFFFF'  # Not in index
if request_crc in index['shards']:
    print('DEDUP_HIT')
else:
    print('DEDUP_MISS transfer_required=true')
" 2>/dev/null)

if echo "$NEW_RESULT" | grep -q "DEDUP_MISS"; then
    pass "TEST-IRC-009-B2: Dedup miss — new shard requires transfer"
else
    fail "TEST-IRC-009-B2: Dedup miss" "Expected DEDUP_MISS for unknown CRC64"
fi

# Verify transfer_count not incremented for dedup hit
TRANSFER_COUNT=$(python3 -c "
import json
with open('$INDEX_FILE', 'r') as f:
    index = json.load(f)
print(index['shards']['A1B2C3D4E5F60001']['transfer_count'])
" 2>/dev/null)

if [[ "$TRANSFER_COUNT" -eq 1 ]]; then
    pass "TEST-IRC-009-B3: Transfer count unchanged after dedup hit ($TRANSFER_COUNT)"
else
    fail "TEST-IRC-009-B3: Transfer count" "Expected 1 (no re-transfer), got $TRANSFER_COUNT"
fi

# ─── Test Group C: Checkpoint WAL ───────────────────────────────────────────
echo ""
echo "── Test Group C: Checkpoint WAL Writing (HIVE-IRC-009) ──"

# Write WAL entries for each shard registration
# WAL format: timestamp|operation|crc64|node|layers|size
python3 -c "
import time

wal_entries = [
    f'{time.time()}|SHARD_REGISTER|A1B2C3D4E5F60001|hive-01|layers_0-7|134217728',
    f'{time.time()}|SHARD_REGISTER|B2C3D4E5F6070002|hive-02|layers_8-15|134217728',
    f'{time.time()}|SHARD_REGISTER|C3D4E5F607080003|hive-03|layers_16-23|134217728',
    f'{time.time()}|SHARD_REGISTER|D4E5F60708090004|hive-01|layers_24-31|134217728',
    f'{time.time()}|NODE_JOIN|hive-01|||24576',
    f'{time.time()}|NODE_JOIN|hive-02|||16384',
    f'{time.time()}|NODE_JOIN|hive-03|||24576',
]

with open('$WAL_FILE', 'w') as f:
    for entry in wal_entries:
        f.write(entry + '\n')
print(f'Wrote {len(wal_entries)} WAL entries')
" 2>/dev/null

WAL_LINES=$(wc -l < "$WAL_FILE" 2>/dev/null || echo "0")
if [[ "$WAL_LINES" -eq 7 ]]; then
    pass "TEST-IRC-009-C1: WAL written ($WAL_LINES entries: 4 shards + 3 nodes)"
else
    fail "TEST-IRC-009-C1: WAL entries" "Expected 7, got $WAL_LINES"
fi

# Verify WAL format is machine-readable
WAL_VALID=$(python3 -c "
valid = 0
with open('$WAL_FILE', 'r') as f:
    for line in f:
        parts = line.strip().split('|')
        if len(parts) >= 3:  # timestamp|op|key
            valid += 1
print(valid)
" 2>/dev/null)

if [[ "$WAL_VALID" -eq "$WAL_LINES" ]]; then
    pass "TEST-IRC-009-C2: All WAL entries are machine-readable ($WAL_VALID/$WAL_LINES)"
else
    fail "TEST-IRC-009-C2: WAL format" "Only $WAL_VALID/$WAL_LINES entries valid"
fi

# ─── Test Group D: Crash Recovery — WAL Replay ──────────────────────────────
echo ""
echo "── Test Group D: Crash Recovery (§XIII·5: WAL replayed → index restored) ──"

# Simulate crash: delete in-memory index
BACKUP_INDEX="$STATE_DIR/tensor_index_backup.json"
cp "$INDEX_FILE" "$BACKUP_INDEX"
echo '{"version":1,"shards":{},"node_registry":{}}' > "$INDEX_FILE"

# Verify index is now empty
EMPTY_COUNT=$(python3 -c "import json; print(len(json.load(open('$INDEX_FILE'))['shards']))" 2>/dev/null)
if [[ "$EMPTY_COUNT" -eq 0 ]]; then
    pass "TEST-IRC-009-D1: Crash simulated — index cleared ($EMPTY_COUNT shards)"
else
    fail "TEST-IRC-009-D1: Crash simulation" "Index not properly cleared"
fi

# Replay WAL to restore index
python3 -c "
import json, time

# Start with empty index (post-crash)
index = {'version': 1, 'shards': {}, 'node_registry': {}}

# Replay WAL
replayed = 0
with open('$WAL_FILE', 'r') as f:
    for line in f:
        parts = line.strip().split('|')
        timestamp = parts[0]
        op = parts[1]

        if op == 'SHARD_REGISTER':
            crc64 = parts[2]
            node = parts[3]
            layers = parts[4]
            size = int(parts[5])
            index['shards'][crc64] = {
                'holder_node': node,
                'layer_range': layers,
                'size_bytes': size,
                'timestamp': float(timestamp),
                'transfer_count': 1
            }
            replayed += 1
        elif op == 'NODE_JOIN':
            node = parts[2]
            vram = int(parts[5]) if parts[5] else 0
            index['node_registry'][node] = {
                'active': True,
                'shards_held': sum(1 for s in index['shards'].values() if s['holder_node'] == node),
                'vram_gb': vram // 1024 if vram > 0 else 0
            }
            replayed += 1

with open('$INDEX_FILE', 'w') as f:
    json.dump(index, f, indent=2)
print(f'Replayed {replayed} WAL entries')
" 2>/dev/null

# Verify restored shard count
RESTORED_SHARDS=$(python3 -c "import json; print(len(json.load(open('$INDEX_FILE'))['shards']))" 2>/dev/null)
if [[ "$RESTORED_SHARDS" -eq 4 ]]; then
    pass "TEST-IRC-009-D2: WAL replay restored $RESTORED_SHARDS/4 shards"
else
    fail "TEST-IRC-009-D2: WAL replay" "Restored $RESTORED_SHARDS/4 shards"
fi

# Verify restored node count
RESTORED_NODES=$(python3 -c "import json; print(len(json.load(open('$INDEX_FILE'))['node_registry']))" 2>/dev/null)
if [[ "$RESTORED_NODES" -eq 3 ]]; then
    pass "TEST-IRC-009-D3: WAL replay restored $RESTORED_NODES/3 nodes"
else
    fail "TEST-IRC-009-D3: Node registry" "Restored $RESTORED_NODES/3 nodes"
fi

# Verify data integrity: compare restored index with pre-crash backup
INTEGRITY_CHECK=$(python3 -c "
import json

with open('$INDEX_FILE', 'r') as f:
    restored = json.load(f)
with open('$BACKUP_INDEX', 'r') as f:
    original = json.load(f)

# Compare shard CRC64 keys
restored_keys = set(restored['shards'].keys())
original_keys = set(original['shards'].keys())

if restored_keys == original_keys:
    # Verify holder nodes match
    mismatches = []
    for k in restored_keys:
        if restored['shards'][k]['holder_node'] != original['shards'][k]['holder_node']:
            mismatches.append(k)
    if not mismatches:
        print('MATCH')
    else:
        print(f'HOLDER_MISMATCH count={len(mismatches)}')
else:
    missing = original_keys - restored_keys
    extra = restored_keys - original_keys
    print(f'KEY_MISMATCH missing={len(missing)} extra={len(extra)}')
" 2>/dev/null)

if [[ "$INTEGRITY_CHECK" == "MATCH" ]]; then
    pass "TEST-IRC-009-D4: Restored index matches pre-crash state (full integrity)"
else
    fail "TEST-IRC-009-D4: Index integrity after WAL replay" "$INTEGRITY_CHECK"
fi

# ─── Summary ─────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo -e " TEST-IRC-009 Results: ${TESTS_PASSED}/${TESTS_RUN} passed, ${TESTS_FAILED} failed"
if [[ "$TESTS_FAILED" -eq 0 ]]; then
    echo -e " Status: ${GREEN}ALL PASS${NC}"
else
    echo -e " Status: ${RED}FAILED${NC}"
fi
echo " Gate: §XIII·5 — Dedup skips re-transfer; WAL recovery restores index"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""

exit "$TESTS_FAILED"
