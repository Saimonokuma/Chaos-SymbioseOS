/*++
 * video_temporal.c — Keyframe Buffer for Temporal Reasoning
 *
 * HIVE-MM-004: Maintains a 16-frame sliding window of keyframes for
 *              temporal video understanding. Enables the AI to reason
 *              about motion, gestures, and events across time.
 *
 * Reference:
 *   - Interactive_Plan.md §XVII·4f (lines 7167-7217) — Video Temporal
 *
 * Architecture:
 *   Standard single-frame vision is limited — the AI needs temporal
 *   context across video frames. This module maintains a circular
 *   buffer of keyframes extracted every Nth frame, each preprocessed
 *   through the vision pipeline (§XVII·4b).
 *
 *   The multi-frame context is sent as a batched request to
 *   llama-server /v1/chat/completions with multiple image_url parts.
 *
 * Acceptance criteria:
 *   16-keyframe circular buffer operational;
 *   Keyframes extracted every 5th frame;
 *   FPS estimation from nanosecond timestamps;
 *   Oldest keyframes freed correctly (no leaks)
 *--*/

#include "multimodal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ═══════════════════════════════════════════════════════════════════════════
// Global video context — singleton sliding window
//
// §XVII·4f line 7185
// ═══════════════════════════════════════════════════════════════════════════

static VIDEO_CONTEXT g_VideoCtx = {0};

// ═══════════════════════════════════════════════════════════════════════════
// video_ingest_frame — Process incoming video frame
//
// §XVII·4f lines 7188-7207
//
// Only keyframes (every Nth frame) are processed through the vision
// pipeline and stored in the circular buffer. Non-keyframes are
// silently dropped to maintain >30fps throughput.
//
// For Moviola mode: ALL frames are processed through delta-motion
// (moviola_delta.c), but only keyframes enter this temporal buffer.
// ═══════════════════════════════════════════════════════════════════════════

void video_ingest_frame(const uint8_t* jpeg, size_t size, uint32_t frame_num)
{
    if (!jpeg || size == 0) return;

    g_VideoCtx.TotalFramesSeen++;

    // Only process keyframes (every Nth frame) — §XVII·4f line 7193
    if (frame_num % KEYFRAME_INTERVAL != 0) return;

    // Preprocess keyframe through vision pipeline (§XVII·4b)
    VISION_FRAME* frame = vision_preprocess(jpeg, size);
    if (!frame) {
        fprintf(stderr, "[VIDEO] WARNING: vision_preprocess failed for "
                "frame %u\n", frame_num);
        return;
    }

    // Circular buffer: overwrite oldest keyframe — §XVII·4f lines 7198-7201
    uint32_t idx = g_VideoCtx.KeyframeCount % VIDEO_KEYFRAME_WINDOW;
    if (g_VideoCtx.Keyframes[idx]) {
        vision_frame_free(g_VideoCtx.Keyframes[idx]);
    }
    g_VideoCtx.Keyframes[idx] = frame;
    g_VideoCtx.KeyframeCount++;

    // Update temporal stats — §XVII·4f lines 7203-7206
    g_VideoCtx.LastFrameTs = frame->TimestampNs;
    if (g_VideoCtx.KeyframeCount == 1) {
        g_VideoCtx.FirstFrameTs = frame->TimestampNs;
    }

    // Estimate FPS from keyframe timestamps
    if (g_VideoCtx.KeyframeCount > 1 &&
        g_VideoCtx.LastFrameTs > g_VideoCtx.FirstFrameTs) {
        uint64_t elapsed_ns = g_VideoCtx.LastFrameTs - g_VideoCtx.FirstFrameTs;
        float elapsed_s = (float)elapsed_ns / 1000000000.0f;
        // FPS based on total frames seen (not just keyframes)
        g_VideoCtx.Fps = (float)g_VideoCtx.TotalFramesSeen / elapsed_s;
    }

    fprintf(stderr, "[VIDEO] Keyframe %u stored (idx=%u, total=%u, "
            "fps=%.1f, tiles=%u)\n",
            frame_num, idx, g_VideoCtx.KeyframeCount,
            g_VideoCtx.Fps, frame->TileCount);
}

// ═══════════════════════════════════════════════════════════════════════════
// video_get_context — Retrieve the current temporal window
//
// Returns a pointer to the global VIDEO_CONTEXT for the LLM to build
// a multi-frame batched inference request.
//
// §XVII·4f lines 7209-7211:
//   "Build multi-frame context for LLM (concatenate keyframe tiles)"
//   "Sent as a single batched request to llama-server /v1/chat/completions"
//   "with multiple image_url content parts representing the temporal sequence"
// ═══════════════════════════════════════════════════════════════════════════

const VIDEO_CONTEXT* video_get_context(void)
{
    return &g_VideoCtx;
}

// ═══════════════════════════════════════════════════════════════════════════
// video_get_active_keyframes — Return count of valid keyframes in buffer
// ═══════════════════════════════════════════════════════════════════════════

uint32_t video_get_active_keyframes(void)
{
    if (g_VideoCtx.KeyframeCount < VIDEO_KEYFRAME_WINDOW)
        return g_VideoCtx.KeyframeCount;
    return VIDEO_KEYFRAME_WINDOW;
}

// ═══════════════════════════════════════════════════════════════════════════
// video_reset — Flush all keyframes and reset temporal state
//
// Called during RECALL_ALL or when switching input sources.
// ═══════════════════════════════════════════════════════════════════════════

void video_reset(void)
{
    for (int i = 0; i < VIDEO_KEYFRAME_WINDOW; i++) {
        if (g_VideoCtx.Keyframes[i]) {
            vision_frame_free(g_VideoCtx.Keyframes[i]);
            g_VideoCtx.Keyframes[i] = NULL;
        }
    }
    memset(&g_VideoCtx, 0, sizeof(VIDEO_CONTEXT));

    // Also reset Moviola delta state (g_PrevGray) to prevent stale comparisons
    moviola_delta_reset();

    fprintf(stderr, "[VIDEO] Temporal context reset\n");
}
