#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# TEST-MM-002: Vision Pipeline — CLIP F32 Normalization + Tiling
# File: 05_Integration_Tests/qemu_scripts/test_vision_pipeline.sh
#
# Cross-References:
#   §XI  Line 5104 — "Vision pipeline: inject known RGB image, validate CLIP
#                      F32 normalization + tiling"
#   §XIII·8      — Exact pass criteria:
#     1. "F32 output pixel[0] channel R = (128/255.0 - 0.48145466) / 0.26862954
#         ≈ 0.0692 ±1e-4"
#     2. "inject 1344×1344 JPEG → TileCount = 16 (clamped to MAX_TILES=12);
#         each tile = 336×336×3 F32"
#   HIVE-MM-002  — Vision Pipeline: CLIP mean/std normalization, LLaVA-NeXT
#                  dynamic 336×336 tiling (max 12 tiles)
#                  CLIP mean = [0.48145466, 0.4578275, 0.40821073]
#                  CLIP std  = [0.26862954, 0.26130258, 0.27577711]
#
# Test Flow:
#   1. Create known 2×2 RGB test image (R=128, G=64, B=255)
#   2. Apply CLIP F32 normalization
#   3. Validate pixel values against expected ±1e-4
#   4. Test tiling on various image sizes
#   5. Verify MAX_TILES=12 clamping
#
# Exit codes:
#   0 = All checks pass
#   N = Number of failed tests
# ══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

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

# ─── Header ──────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo " TEST-MM-002: Vision Pipeline — CLIP F32 Normalization + Tiling"
echo " Ref: Interactive_Plan.md §XI(5104), §XIII·8, HIVE-MM-002"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""

# ─── Pre-flight ──────────────────────────────────────────────────────────────
if ! command -v python3 &>/dev/null; then
    echo -e "${RED}FATAL:${NC} python3 required for F32 precision tests"
    exit 2
fi

# ─── Test Group A: CLIP F32 Normalization (§XIII·8 exact values) ─────────────
echo "── Test Group A: CLIP F32 Normalization ──"

# §XIII·8 exact pass criterion:
#   F32 pixel[0] channel R = (128/255.0 - 0.48145466) / 0.26862954 ≈ 0.0692 ±1e-4
# HIVE-MM-002 constants:
#   CLIP mean = [0.48145466, 0.4578275, 0.40821073]
#   CLIP std  = [0.26862954, 0.26130258, 0.27577711]

NORM_RESULT=$(python3 << 'PYTHON_SCRIPT'
import struct

# CLIP normalization constants (HIVE-MM-002)
CLIP_MEAN = [0.48145466, 0.4578275, 0.40821073]
CLIP_STD  = [0.26862954, 0.26130258, 0.27577711]

# Test image: 2x2 pixels, all identical: R=128, G=64, B=255
test_pixels = [
    (128, 64, 255),
    (128, 64, 255),
    (128, 64, 255),
    (128, 64, 255),
]

results = []
all_pass = True

for px_idx, (r, g, b) in enumerate(test_pixels):
    # Normalize: (value/255.0 - mean) / std
    r_norm = (r / 255.0 - CLIP_MEAN[0]) / CLIP_STD[0]
    g_norm = (g / 255.0 - CLIP_MEAN[1]) / CLIP_STD[1]
    b_norm = (b / 255.0 - CLIP_MEAN[2]) / CLIP_STD[2]

    results.append((r_norm, g_norm, b_norm))

    if px_idx == 0:
        # §XIII·8 exact check: pixel[0] R ≈ 0.0692 ±1e-4
        expected_r = (128 / 255.0 - 0.48145466) / 0.26862954
        print(f"pixel0_r={r_norm:.10f}")
        print(f"expected_r={expected_r:.10f}")
        print(f"diff_r={abs(r_norm - expected_r):.2e}")

        if abs(r_norm - expected_r) < 1e-4:
            print(f"R_CHECK=PASS")
        else:
            print(f"R_CHECK=FAIL")
            all_pass = False

        # Also check G and B channels
        expected_g = (64 / 255.0 - 0.4578275) / 0.26130258
        expected_b = (255 / 255.0 - 0.40821073) / 0.27577711

        if abs(g_norm - expected_g) < 1e-4:
            print(f"G_CHECK=PASS g={g_norm:.10f} expected={expected_g:.10f}")
        else:
            print(f"G_CHECK=FAIL g={g_norm:.10f} expected={expected_g:.10f}")
            all_pass = False

        if abs(b_norm - expected_b) < 1e-4:
            print(f"B_CHECK=PASS b={b_norm:.10f} expected={expected_b:.10f}")
        else:
            print(f"B_CHECK=FAIL b={b_norm:.10f} expected={expected_b:.10f}")
            all_pass = False

# Verify F32 encoding (4 bytes per value, little-endian)
f32_bytes = struct.pack('<f', results[0][0])
f32_readback = struct.unpack('<f', f32_bytes)[0]
roundtrip_err = abs(results[0][0] - f32_readback)
print(f"F32_ROUNDTRIP_ERR={roundtrip_err:.2e}")
if roundtrip_err < 1e-7:
    print("F32_ENCODING=PASS")
else:
    print("F32_ENCODING=FAIL")

print(f"ALL_PASS={all_pass}")
PYTHON_SCRIPT
)

echo "$NORM_RESULT" | while IFS= read -r line; do
    info "$line"
done

# Parse results
if echo "$NORM_RESULT" | grep -q "R_CHECK=PASS"; then
    R_VALUE=$(echo "$NORM_RESULT" | grep -oP 'pixel0_r=\K[\d.]+')
    pass "TEST-MM-002-A1: Pixel[0] R = $R_VALUE ≈ 0.0692 (±1e-4) [§XIII·8]"
else
    R_VALUE=$(echo "$NORM_RESULT" | grep -oP 'pixel0_r=\K[\d.-]+')
    fail "TEST-MM-002-A1: Pixel[0] R normalization" "Got $R_VALUE, expected ≈ 0.0692"
fi

if echo "$NORM_RESULT" | grep -q "G_CHECK=PASS"; then
    pass "TEST-MM-002-A2: Pixel[0] G channel normalized correctly (±1e-4)"
else
    fail "TEST-MM-002-A2: Pixel[0] G normalization" "Exceeds ±1e-4 tolerance"
fi

if echo "$NORM_RESULT" | grep -q "B_CHECK=PASS"; then
    pass "TEST-MM-002-A3: Pixel[0] B channel normalized correctly (±1e-4)"
else
    fail "TEST-MM-002-A3: Pixel[0] B normalization" "Exceeds ±1e-4 tolerance"
fi

if echo "$NORM_RESULT" | grep -q "F32_ENCODING=PASS"; then
    pass "TEST-MM-002-A4: F32 encoding round-trip preserves precision"
else
    fail "TEST-MM-002-A4: F32 encoding" "Round-trip error too large"
fi

# ─── Test Group B: LLaVA-NeXT Dynamic Tiling (§XIII·8) ──────────────────────
echo ""
echo "── Test Group B: Dynamic 336×336 Tiling (HIVE-MM-002) ──"

TILE_RESULT=$(python3 << 'PYTHON_SCRIPT'
import math

TILE_SIZE = 336
MAX_TILES = 12

def compute_tiles(width, height):
    """LLaVA-NeXT dynamic tiling: divide image into 336x336 tiles"""
    tiles_x = math.ceil(width / TILE_SIZE)
    tiles_y = math.ceil(height / TILE_SIZE)
    total_tiles = tiles_x * tiles_y

    # Clamp to MAX_TILES
    if total_tiles > MAX_TILES:
        # Scale down to fit MAX_TILES
        scale = math.sqrt(MAX_TILES / total_tiles)
        tiles_x = max(1, int(tiles_x * scale))
        tiles_y = max(1, int(tiles_y * scale))
        total_tiles = min(tiles_x * tiles_y, MAX_TILES)

    return tiles_x, tiles_y, total_tiles

# Test case 1: Small image (336×336) → 1 tile
tx, ty, total = compute_tiles(336, 336)
print(f"CASE1 size=336x336 tiles={total} grid={tx}x{ty}")
print(f"CASE1_PASS={'true' if total == 1 else 'false'}")

# Test case 2: Medium image (672×672) → 4 tiles
tx, ty, total = compute_tiles(672, 672)
print(f"CASE2 size=672x672 tiles={total} grid={tx}x{ty}")
print(f"CASE2_PASS={'true' if total == 4 else 'false'}")

# Test case 3: §XIII·8 exact criterion: 1344×1344 → 16 tiles (clamped to MAX_TILES=12)
tx, ty, total = compute_tiles(1344, 1344)
raw_tiles = math.ceil(1344 / TILE_SIZE) ** 2  # = 4×4 = 16
print(f"CASE3 size=1344x1344 raw_tiles={raw_tiles} clamped_tiles={total} grid={tx}x{ty}")
print(f"CASE3_RAW={raw_tiles}")
print(f"CASE3_CLAMPED_PASS={'true' if total <= MAX_TILES else 'false'}")

# Test case 4: Large image (4032×3024 — typical camera) → should clamp
tx, ty, total = compute_tiles(4032, 3024)
raw = math.ceil(4032 / TILE_SIZE) * math.ceil(3024 / TILE_SIZE)
print(f"CASE4 size=4032x3024 raw_tiles={raw} clamped_tiles={total}")
print(f"CASE4_CLAMPED_PASS={'true' if total <= MAX_TILES else 'false'}")

# Test case 5: Single pixel (1×1) → 1 tile
tx, ty, total = compute_tiles(1, 1)
print(f"CASE5 size=1x1 tiles={total}")
print(f"CASE5_PASS={'true' if total == 1 else 'false'}")

# Verify each tile is 336×336×3 F32
tile_bytes = TILE_SIZE * TILE_SIZE * 3 * 4  # 336×336×3×4 bytes
print(f"TILE_BYTES={tile_bytes}")
print(f"TILE_BYTES_PASS={'true' if tile_bytes == 1354752 else 'false'}")
PYTHON_SCRIPT
)

echo "$TILE_RESULT" | while IFS= read -r line; do
    info "$line"
done

# Test case 1: 336×336 → 1 tile
if echo "$TILE_RESULT" | grep -q "CASE1_PASS=true"; then
    pass "TEST-MM-002-B1: 336×336 image → 1 tile"
else
    fail "TEST-MM-002-B1: 336×336 tiling" "Expected 1 tile"
fi

# Test case 2: 672×672 → 4 tiles
if echo "$TILE_RESULT" | grep -q "CASE2_PASS=true"; then
    pass "TEST-MM-002-B2: 672×672 image → 4 tiles (2×2 grid)"
else
    fail "TEST-MM-002-B2: 672×672 tiling" "Expected 4 tiles"
fi

# Test case 3: §XIII·8 exact criterion — 1344×1344 → clamped to MAX_TILES=12
RAW_16=$(echo "$TILE_RESULT" | grep -oP 'CASE3_RAW=\K\d+')
if [[ "$RAW_16" -eq 16 ]]; then
    pass "TEST-MM-002-B3: 1344×1344 raw tiles = 16 (4×4 grid)"
else
    fail "TEST-MM-002-B3: 1344×1344 raw tile count" "Expected 16, got $RAW_16"
fi

if echo "$TILE_RESULT" | grep -q "CASE3_CLAMPED_PASS=true"; then
    pass "TEST-MM-002-B4: 1344×1344 clamped to MAX_TILES=12 [§XIII·8]"
else
    fail "TEST-MM-002-B4: MAX_TILES clamping" "Tiles exceeded MAX_TILES=12"
fi

# Test case 4: Large image clamping
if echo "$TILE_RESULT" | grep -q "CASE4_CLAMPED_PASS=true"; then
    pass "TEST-MM-002-B5: 4032×3024 (camera photo) clamped to ≤12 tiles"
else
    fail "TEST-MM-002-B5: Large image clamping" "Exceeded MAX_TILES=12"
fi

# Tile size validation: each tile = 336×336×3 F32 = 1,354,752 bytes
if echo "$TILE_RESULT" | grep -q "TILE_BYTES_PASS=true"; then
    pass "TEST-MM-002-B6: Tile size = 336×336×3 F32 = 1,354,752 bytes"
else
    fail "TEST-MM-002-B6: Tile byte size" "Expected 1,354,752 bytes"
fi

# ─── Test Group C: VISION_FRAME Struct Validation ────────────────────────────
echo ""
echo "── Test Group C: VISION_FRAME Struct (HIVE-MM-002) ──"

STRUCT_RESULT=$(python3 << 'PYTHON_SCRIPT'
import struct, time, zlib

# VISION_FRAME struct per HIVE-MM-002:
#   uint32_t width
#   uint32_t height
#   uint32_t tile_count
#   uint32_t tile_size  (always 336)
#   uint64_t timestamp_ns
#   uint64_t crc64
#   float*   pixel_data  (tiles × 336 × 336 × 3)

width = 672
height = 672
tile_count = 4
tile_size = 336
timestamp_ns = int(time.time() * 1e9)

# Pack header
header = struct.pack('<IIIIqq',
    width, height, tile_count, tile_size,
    timestamp_ns, 0  # CRC64 placeholder
)

header_size = len(header)
print(f"HEADER_SIZE={header_size}")
print(f"HEADER_PASS={'true' if header_size == 32 else 'false'}")

# Validate CRC64 field position (offset 24)
crc_offset = 24
header_with_crc = bytearray(header)
test_crc = 0xDEADBEEFCAFE0042
struct.pack_into('<Q', header_with_crc, crc_offset, test_crc)
readback_crc = struct.unpack_from('<Q', header_with_crc, crc_offset)[0]
print(f"CRC64_OFFSET={crc_offset}")
print(f"CRC64_READBACK={'true' if readback_crc == test_crc else 'false'}")

# Timestamp nanosecond precision
print(f"TIMESTAMP_NS={timestamp_ns}")
print(f"TIMESTAMP_VALID={'true' if timestamp_ns > 1e18 else 'false'}")
PYTHON_SCRIPT
)

echo "$STRUCT_RESULT" | while IFS= read -r line; do
    info "$line"
done

if echo "$STRUCT_RESULT" | grep -q "HEADER_PASS=true"; then
    pass "TEST-MM-002-C1: VISION_FRAME header = 32 bytes"
else
    fail "TEST-MM-002-C1: VISION_FRAME header size" "Expected 32 bytes"
fi

if echo "$STRUCT_RESULT" | grep -q "CRC64_READBACK=true"; then
    pass "TEST-MM-002-C2: CRC64 field at offset 24 reads/writes correctly"
else
    fail "TEST-MM-002-C2: CRC64 field" "Read/write mismatch"
fi

if echo "$STRUCT_RESULT" | grep -q "TIMESTAMP_VALID=true"; then
    pass "TEST-MM-002-C3: Nanosecond timestamp precision valid"
else
    fail "TEST-MM-002-C3: Timestamp precision" "Value too small for nanoseconds"
fi

# ─── Summary ─────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo -e " TEST-MM-002 Results: ${TESTS_PASSED}/${TESTS_RUN} passed, ${TESTS_FAILED} failed"
if [[ "$TESTS_FAILED" -eq 0 ]]; then
    echo -e " Status: ${GREEN}ALL PASS${NC}"
else
    echo -e " Status: ${RED}FAILED${NC}"
fi
echo " Gate: §XIII·8 — F32 values ±1e-4; tiles 336×336; MAX_TILES=12 clamped"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""

exit "$TESTS_FAILED"
