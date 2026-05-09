/*++
 * moviola_delta.c — Neuromorphic Delta-Motion Vision Mode
 *
 * HIVE-MM-005: Software frame-differencing for Moviola Protocol.
 *              Produces 1-bit change-maps at >90fps with >99% sparsity.
 *              Packs delta-motion into 10×10 micro-grid tokens for
 *              Di-Bit LLM injection.
 *
 * Reference:
 *   - Interactive_Plan.md §XVII·4g (lines 7319-7431) — Moviola Delta
 *   - Evaluating_Moviola_project_Architecture.md §4.2
 *
 * Architecture:
 *   Standard vision (§XVII·4b) uses dense 24-bit RGB — ideal for
 *   static images but wasteful for video where >99% of pixels are
 *   unchanged. Moviola activates a delta-motion channel that processes
 *   only pixel changes, achieving neuromorphic-grade efficiency.
 *
 *   Pipeline:
 *     1. Convert frame to grayscale
 *     2. |current - previous| > threshold → 1-bit change-map
 *     3. Pack into 10×10 micro-grid tokens
 *     4. Route to Di-Bit injection (§XVII·4h) or vision fallback
 *
 *   Typical sparsity: >99.5% for static scenes
 *   Computational complexity: O(nnz) instead of O(n²) dense attention
 *
 * Acceptance criteria:
 *   Delta computation at >90fps for 640×480;
 *   Sparsity correctly computed;
 *   Micro-grid packing produces correct bit patterns;
 *   IRC MOVIOLA_DELTA message emitted
 *--*/

#include "multimodal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ── Previous frame buffer (grayscale) ──────────────────────────────────────
static uint8_t* g_PrevGray = NULL;
static uint32_t g_PrevWidth = 0;
static uint32_t g_PrevHeight = 0;

// ═══════════════════════════════════════════════════════════════════════════
// moviola_compute_delta — Compute delta-motion frame
//
// §XVII·4g lines 7354-7389
//
// Input: grayscale frame (w × h, 1 byte per pixel)
// Output: DELTA_FRAME with 1-bit change-map
//
// First frame: all pixels marked as "change" (entire frame is new)
// Subsequent: |current[i] - previous[i]| > DELTA_THRESHOLD → bit set
//
// Returns heap-allocated DELTA_FRAME. Caller must call delta_frame_free().
// ═══════════════════════════════════════════════════════════════════════════

DELTA_FRAME* moviola_compute_delta(const uint8_t* gray_frame,
                                     uint32_t w, uint32_t h)
{
    if (!gray_frame || w == 0 || h == 0) return NULL;

    DELTA_FRAME* df = calloc(1, sizeof(DELTA_FRAME));
    if (!df) return NULL;

    df->Width  = w;
    df->Height = h;
    size_t npix = (size_t)w * h;

    // Bit-packed change-map: 1 bit per pixel — §XVII·4g line 7361
    size_t map_bytes = (npix + 7) / 8;
    df->ChangeMap = calloc(map_bytes, 1);
    if (!df->ChangeMap) {
        free(df);
        return NULL;
    }

    // First frame: all pixels are "change" — §XVII·4g lines 7363-7371
    if (g_PrevGray == NULL || g_PrevWidth != w || g_PrevHeight != h) {
        g_PrevGray = malloc(npix);
        if (!g_PrevGray) {
            free(df->ChangeMap);
            free(df);
            return NULL;
        }
        memcpy(g_PrevGray, gray_frame, npix);
        g_PrevWidth  = w;
        g_PrevHeight = h;

        memset(df->ChangeMap, 0xFF, map_bytes);
        df->ActivePixels = npix;
        df->Sparsity = 0.0f;
        df->TimestampNs = clock_gettime_ns(CLOCK_MONOTONIC);
        return df;
    }

    // Delta computation: |current - previous| > threshold → 1
    // §XVII·4g lines 7373-7381
    // Threshold is configurable via model.conf moviola_delta_threshold
    int threshold = g_MmConfig.moviola_delta_threshold > 0
                    ? g_MmConfig.moviola_delta_threshold
                    : DELTA_THRESHOLD;  // Fallback to compiled default (15)
    df->ActivePixels = 0;
    for (size_t i = 0; i < npix; i++) {
        int delta = abs((int)gray_frame[i] - (int)g_PrevGray[i]);
        if (delta > threshold) {
            df->ChangeMap[i / 8] |= (1 << (i % 8));
            df->ActivePixels++;
        }
    }

    // Update previous frame buffer — §XVII·4g line 7382
    memcpy(g_PrevGray, gray_frame, npix);

    // Compute sparsity — §XVII·4g line 7384
    df->Sparsity = 1.0f - ((float)df->ActivePixels / (float)npix);
    df->TimestampNs = clock_gettime_ns(CLOCK_MONOTONIC);

    return df;
}

// ═══════════════════════════════════════════════════════════════════════════
// moviola_extract_grid — Extract a single 10×10 micro-grid from change-map
//
// §XVII·4g lines 7394-7408
//
// Returns a byte where each bit represents whether the corresponding
// pixel in the micro-grid has changed. For full Di-Bit encoding,
// see moviola_dibit.c.
// ═══════════════════════════════════════════════════════════════════════════

uint8_t moviola_extract_grid(const uint8_t* change_map, uint32_t width,
                              uint32_t gx, uint32_t gy)
{
    if (!change_map) return 0;

    uint8_t result = 0;
    int bit = 0;

    uint32_t base_x = gx * MOVIOLA_MICROGRID;
    uint32_t base_y = gy * MOVIOLA_MICROGRID;

    // Sample 8 representative pixels from the 10×10 grid
    // (packed into a single byte for the simple packing path)
    for (int dy = 0; dy < MOVIOLA_MICROGRID && bit < 8; dy += 3) {
        for (int dx = 0; dx < MOVIOLA_MICROGRID && bit < 8; dx += 3) {
            uint32_t px = base_x + dx;
            uint32_t py = base_y + dy;
            size_t idx = (size_t)py * width + px;

            if (change_map[idx / 8] & (1 << (idx % 8))) {
                result |= (1 << bit);
            }
            bit++;
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// moviola_pack_dibit — Pack delta-motion into micro-grid tokens
//
// §XVII·4g lines 7394-7409
//
// Each 10×10 micro-grid → single byte token (simple packing).
// For full 25-byte Di-Bit encoding, see moviola_dibit.c (HIVE-MM-009).
// ═══════════════════════════════════════════════════════════════════════════

uint32_t moviola_pack_dibit(const DELTA_FRAME* df, uint8_t* token_buf,
                             size_t buf_size)
{
    if (!df || !token_buf || buf_size == 0) return 0;

    uint32_t grid_cols = (df->Width  + MOVIOLA_MICROGRID - 1) / MOVIOLA_MICROGRID;
    uint32_t grid_rows = (df->Height + MOVIOLA_MICROGRID - 1) / MOVIOLA_MICROGRID;
    uint32_t tokens = 0;

    for (uint32_t gy = 0; gy < grid_rows && tokens < buf_size; gy++) {
        for (uint32_t gx = 0; gx < grid_cols && tokens < buf_size; gx++) {
            token_buf[tokens++] = moviola_extract_grid(
                df->ChangeMap, df->Width, gx, gy);
        }
    }

    return tokens;
}

// ═══════════════════════════════════════════════════════════════════════════
// delta_frame_free — Release all resources in a DELTA_FRAME
// ═══════════════════════════════════════════════════════════════════════════

void delta_frame_free(DELTA_FRAME* df)
{
    if (!df) return;
    free(df->ChangeMap);
    free(df);
}

// ═══════════════════════════════════════════════════════════════════════════
// moviola_delta_reset — Release previous-frame buffer and reset state
//
// Called during RECALL_ALL or video_reset() to ensure the next frame
// is treated as a fresh start (no stale delta comparison).
// ═══════════════════════════════════════════════════════════════════════════

void moviola_delta_reset(void)
{
    free(g_PrevGray);
    g_PrevGray = NULL;
    g_PrevWidth = 0;
    g_PrevHeight = 0;

    fprintf(stderr, "[MOVIOLA] Delta state reset\n");
}
