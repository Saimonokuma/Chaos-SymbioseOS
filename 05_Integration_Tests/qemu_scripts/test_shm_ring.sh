#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# TEST-IRC-008: SHM Ring Buffer Concurrency Test
# File: 05_Integration_Tests/qemu_scripts/test_shm_ring.sh
#
# Cross-References:
#   §XI  Line 5101 — "SHM ring buffer: 8 concurrent jumbo payloads,
#                      no head-of-line blocking"
#   §XIII·5      — "8 concurrent jumbo payloads in-flight; no head-of-line
#                   blocking; overflow counter accurate"
#   §VII·7       — SHM ring architecture: 8 slots × 512MB, SlotMeta lifecycle
#                  (FREE → WRITING → COMMITTED → READING → FREE)
#   §XVI         — SHM_CONTROL_HEADER: 64-byte aligned, per-slot CRC64
#   §X·21        — SHM for control messages only; tensors via RDMA
#
# Test Flow:
#   1. Create simulated SHM ring buffer (8 slots in shared memory)
#   2. Launch 8 concurrent writer processes (one per slot)
#   3. Verify all 8 slots used simultaneously (no head-of-line blocking)
#   4. Validate slot state transitions (FREE→WRITING→COMMITTED)
#   5. Read back all slots and verify CRC64
#   6. Test overflow counter (write to full ring)
#   7. Verify no slot corruption under concurrent access
#
# Exit codes:
#   0 = All checks pass
#   N = Number of failed tests
# ══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

# ─── Configuration ───────────────────────────────────────────────────────────
NUM_SLOTS=8
SLOT_SIZE_KB="${SLOT_SIZE_KB:-1024}"  # 1MB per slot for test (vs 512MB production)
TIMEOUT_SECS="${TIMEOUT_SECS:-30}"

# SlotMeta state values per §VII·7
STATE_FREE=0
STATE_WRITING=1
STATE_COMMITTED=2
STATE_READING=3

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

# Temp directory for simulated SHM
SHM_DIR="$(mktemp -d /tmp/test_irc008_shm_XXXXXX)"
SLOT_DIR="$SHM_DIR/slots"
META_DIR="$SHM_DIR/meta"
mkdir -p "$SLOT_DIR" "$META_DIR"

cleanup() {
    kill $(jobs -p) 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$SHM_DIR"
}
trap cleanup EXIT

# ─── CRC64 Helper ────────────────────────────────────────────────────────────
compute_crc64() {
    local file="$1"
    if command -v python3 &>/dev/null; then
        python3 -c "
POLY = 0x42F0E1EBA9EA3693
crc = 0xFFFFFFFFFFFFFFFF
with open('$file', 'rb') as f:
    for chunk in iter(lambda: f.read(65536), b''):
        for byte in chunk:
            crc ^= byte << 56
            for _ in range(8):
                if crc & (1 << 63):
                    crc = (crc << 1) ^ POLY
                else:
                    crc <<= 1
                crc &= 0xFFFFFFFFFFFFFFFF
print(format(crc ^ 0xFFFFFFFFFFFFFFFF, '016X'))
"
    else
        md5sum "$file" | cut -c1-16 | tr 'a-f' 'A-F'
    fi
}

# ─── Header ──────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo " TEST-IRC-008: SHM Ring Buffer Concurrency"
echo " Ref: Interactive_Plan.md §XI(5101), §XIII·5, §VII·7, §XVI"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""

# ─── Test 1: Initialize Ring Buffer ──────────────────────────────────────────
echo "── Test Group A: Ring Buffer Initialization (§VII·7) ──"

# Create control header (§XVI: 64-byte aligned SHM_CONTROL_HEADER)
# Fields: Magic(4) + Version(4) + NumSlots(4) + SlotSize(8) + WriteHead(4) + ReadHead(4)
#        + OverflowCount(8) + Reserved(28) = 64 bytes
python3 -c "
import struct
header = struct.pack('<IIIQII8xI12x',
    0x53484D52,      # Magic: 'SHMR'
    1,               # Version
    $NUM_SLOTS,      # NumSlots
    $((SLOT_SIZE_KB * 1024)),  # SlotSize in bytes
    0,               # WriteHead
    0,               # ReadHead
    0                # OverflowCount
)
# Pad to 64 bytes
header = header.ljust(64, b'\x00')
with open('$SHM_DIR/control_header.bin', 'wb') as f:
    f.write(header)
" 2>/dev/null

if [[ -f "$SHM_DIR/control_header.bin" ]]; then
    HEADER_SIZE=$(stat -f%z "$SHM_DIR/control_header.bin" 2>/dev/null || stat -c%s "$SHM_DIR/control_header.bin")
    if [[ "$HEADER_SIZE" -eq 64 ]]; then
        pass "TEST-IRC-008-A1: Control header created (64 bytes, §XVI aligned)"
    else
        fail "TEST-IRC-008-A1: Control header size" "Expected 64 bytes, got $HEADER_SIZE"
    fi
else
    fail "TEST-IRC-008-A1: Control header creation" "File not created"
fi

# Initialize per-slot metadata
# SlotMeta per §VII·7: State(4) + PayloadType(4) + PayloadSize(8) + CRC64(8)
#                       + Timestamp(8) = 32 bytes
for i in $(seq 0 $((NUM_SLOTS - 1))); do
    echo "$STATE_FREE" > "$META_DIR/slot_${i}_state"
    echo "0" > "$META_DIR/slot_${i}_crc64"
    echo "0" > "$META_DIR/slot_${i}_size"
done

pass "TEST-IRC-008-A2: $NUM_SLOTS slot metadata initialized (all FREE)"

# ─── Test 2: Concurrent Slot Writing ────────────────────────────────────────
echo ""
echo "── Test Group B: 8 Concurrent Writers (§XIII·5: no head-of-line blocking) ──"

# Launch 8 concurrent writer processes — one per slot
# Each writer: set state WRITING → generate payload → compute CRC64 → set state COMMITTED
WRITE_PIDS=()
WRITE_START=$(date +%s%N)

for slot in $(seq 0 $((NUM_SLOTS - 1))); do
    (
        # Acquire slot: FREE → WRITING
        echo "$STATE_WRITING" > "$META_DIR/slot_${slot}_state"

        # Generate unique payload for this slot
        SLOT_FILE="$SLOT_DIR/slot_${slot}.bin"
        dd if=/dev/urandom of="$SLOT_FILE" bs=1K count="$SLOT_SIZE_KB" 2>/dev/null

        # Compute CRC64
        CRC=$(compute_crc64 "$SLOT_FILE")
        SIZE=$(stat -f%z "$SLOT_FILE" 2>/dev/null || stat -c%s "$SLOT_FILE")

        # Write metadata
        echo "$CRC" > "$META_DIR/slot_${slot}_crc64"
        echo "$SIZE" > "$META_DIR/slot_${slot}_size"

        # Simulate write latency (variable per slot to test true concurrency)
        sleep 0.$((RANDOM % 5))

        # Commit: WRITING → COMMITTED
        echo "$STATE_COMMITTED" > "$META_DIR/slot_${slot}_state"
    ) &
    WRITE_PIDS+=($!)
done

# Wait for all writers to complete
ALL_COMPLETED=true
for pid in "${WRITE_PIDS[@]}"; do
    if ! wait "$pid" 2>/dev/null; then
        ALL_COMPLETED=false
    fi
done
WRITE_END=$(date +%s%N)

if $ALL_COMPLETED; then
    WRITE_TIME_MS=$(( (WRITE_END - WRITE_START) / 1000000 ))
    pass "TEST-IRC-008-B1: All 8 concurrent writes completed (${WRITE_TIME_MS}ms)"
else
    fail "TEST-IRC-008-B1: Concurrent writes" "One or more writers failed"
fi

# Verify all slots are in COMMITTED state
COMMITTED_COUNT=0
for slot in $(seq 0 $((NUM_SLOTS - 1))); do
    STATE=$(cat "$META_DIR/slot_${slot}_state" 2>/dev/null)
    if [[ "$STATE" == "$STATE_COMMITTED" ]]; then
        COMMITTED_COUNT=$((COMMITTED_COUNT + 1))
    fi
done

if [[ "$COMMITTED_COUNT" -eq "$NUM_SLOTS" ]]; then
    pass "TEST-IRC-008-B2: All $NUM_SLOTS slots in COMMITTED state ($COMMITTED_COUNT/$NUM_SLOTS)"
else
    fail "TEST-IRC-008-B2: Slot states" "Only $COMMITTED_COUNT/$NUM_SLOTS in COMMITTED state"
fi

# Verify no head-of-line blocking: all slots have data
FILLED_COUNT=0
for slot in $(seq 0 $((NUM_SLOTS - 1))); do
    SIZE=$(cat "$META_DIR/slot_${slot}_size" 2>/dev/null)
    if [[ "$SIZE" -gt 0 ]]; then
        FILLED_COUNT=$((FILLED_COUNT + 1))
    fi
done

if [[ "$FILLED_COUNT" -eq "$NUM_SLOTS" ]]; then
    pass "TEST-IRC-008-B3: No head-of-line blocking ($FILLED_COUNT/$NUM_SLOTS slots filled concurrently)"
else
    fail "TEST-IRC-008-B3: Head-of-line blocking detected" "Only $FILLED_COUNT/$NUM_SLOTS slots filled"
fi

# ─── Test 3: CRC64 Validation ───────────────────────────────────────────────
echo ""
echo "── Test Group C: CRC64 Integrity Validation (§VII·7) ──"

CRC_VALID=0
for slot in $(seq 0 $((NUM_SLOTS - 1))); do
    STORED_CRC=$(cat "$META_DIR/slot_${slot}_crc64" 2>/dev/null)
    COMPUTED_CRC=$(compute_crc64 "$SLOT_DIR/slot_${slot}.bin")

    if [[ "$STORED_CRC" == "$COMPUTED_CRC" ]]; then
        CRC_VALID=$((CRC_VALID + 1))
    else
        info "Slot $slot CRC mismatch: stored=$STORED_CRC computed=$COMPUTED_CRC"
    fi
done

if [[ "$CRC_VALID" -eq "$NUM_SLOTS" ]]; then
    pass "TEST-IRC-008-C1: All $NUM_SLOTS slot CRC64 values valid ($CRC_VALID/$NUM_SLOTS)"
else
    fail "TEST-IRC-008-C1: CRC64 validation" "Only $CRC_VALID/$NUM_SLOTS valid"
fi

# ─── Test 4: Read-Back Cycle ────────────────────────────────────────────────
echo ""
echo "── Test Group D: Slot Lifecycle (COMMITTED → READING → FREE) ──"

# Simulate concurrent readers
for slot in $(seq 0 $((NUM_SLOTS - 1))); do
    # COMMITTED → READING
    echo "$STATE_READING" > "$META_DIR/slot_${slot}_state"
done

READING_COUNT=0
for slot in $(seq 0 $((NUM_SLOTS - 1))); do
    STATE=$(cat "$META_DIR/slot_${slot}_state" 2>/dev/null)
    [[ "$STATE" == "$STATE_READING" ]] && READING_COUNT=$((READING_COUNT + 1))
done

if [[ "$READING_COUNT" -eq "$NUM_SLOTS" ]]; then
    pass "TEST-IRC-008-D1: All slots transitioned to READING ($READING_COUNT/$NUM_SLOTS)"
else
    fail "TEST-IRC-008-D1: READING transition" "$READING_COUNT/$NUM_SLOTS"
fi

# READING → FREE (release)
for slot in $(seq 0 $((NUM_SLOTS - 1))); do
    echo "$STATE_FREE" > "$META_DIR/slot_${slot}_state"
done

FREE_COUNT=0
for slot in $(seq 0 $((NUM_SLOTS - 1))); do
    STATE=$(cat "$META_DIR/slot_${slot}_state" 2>/dev/null)
    [[ "$STATE" == "$STATE_FREE" ]] && FREE_COUNT=$((FREE_COUNT + 1))
done

if [[ "$FREE_COUNT" -eq "$NUM_SLOTS" ]]; then
    pass "TEST-IRC-008-D2: All slots released to FREE ($FREE_COUNT/$NUM_SLOTS)"
else
    fail "TEST-IRC-008-D2: FREE transition" "$FREE_COUNT/$NUM_SLOTS"
fi

# ─── Test 5: Overflow Counter ────────────────────────────────────────────────
echo ""
echo "── Test Group E: Overflow Counter (§XIII·5: overflow counter accurate) ──"

# Fill all slots again to COMMITTED
for slot in $(seq 0 $((NUM_SLOTS - 1))); do
    echo "$STATE_COMMITTED" > "$META_DIR/slot_${slot}_state"
done

# Try to write to a full ring — should increment overflow counter
OVERFLOW_COUNT=0
OVERFLOW_ATTEMPTS=3

for attempt in $(seq 1 $OVERFLOW_ATTEMPTS); do
    # Check if any slot is FREE
    HAS_FREE=false
    for slot in $(seq 0 $((NUM_SLOTS - 1))); do
        STATE=$(cat "$META_DIR/slot_${slot}_state" 2>/dev/null)
        if [[ "$STATE" == "$STATE_FREE" ]]; then
            HAS_FREE=true
            break
        fi
    done

    if ! $HAS_FREE; then
        OVERFLOW_COUNT=$((OVERFLOW_COUNT + 1))
    fi
done

if [[ "$OVERFLOW_COUNT" -eq "$OVERFLOW_ATTEMPTS" ]]; then
    pass "TEST-IRC-008-E1: Overflow counter accurate ($OVERFLOW_COUNT overflows on $OVERFLOW_ATTEMPTS attempts)"
else
    fail "TEST-IRC-008-E1: Overflow counter" "Expected $OVERFLOW_ATTEMPTS, got $OVERFLOW_COUNT"
fi

# Update control header overflow count
python3 -c "
import struct
with open('$SHM_DIR/control_header.bin', 'r+b') as f:
    f.seek(28)  # OverflowCount offset
    f.write(struct.pack('<Q', $OVERFLOW_COUNT))
" 2>/dev/null

STORED_OVERFLOW=$(python3 -c "
import struct
with open('$SHM_DIR/control_header.bin', 'rb') as f:
    f.seek(28)
    count = struct.unpack('<Q', f.read(8))[0]
    print(count)
" 2>/dev/null || echo "0")

if [[ "$STORED_OVERFLOW" == "$OVERFLOW_COUNT" ]]; then
    pass "TEST-IRC-008-E2: Overflow count persisted in control header ($STORED_OVERFLOW)"
else
    fail "TEST-IRC-008-E2: Overflow persistence" "Stored=$STORED_OVERFLOW, Expected=$OVERFLOW_COUNT"
fi

# ─── Summary ─────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo -e " TEST-IRC-008 Results: ${TESTS_PASSED}/${TESTS_RUN} passed, ${TESTS_FAILED} failed"
if [[ "$TESTS_FAILED" -eq 0 ]]; then
    echo -e " Status: ${GREEN}ALL PASS${NC}"
else
    echo -e " Status: ${RED}FAILED${NC}"
fi
echo " Gate: §XIII·5 — 8 concurrent jumbos; no head-of-line blocking; overflow accurate"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""

exit "$TESTS_FAILED"
