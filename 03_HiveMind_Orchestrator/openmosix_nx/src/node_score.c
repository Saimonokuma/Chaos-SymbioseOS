/*++
 * node_score.c — Composite Node Scoring Function
 *
 * HIVE-MOSIX-004: Weight-proportional load balancing for tensor sharding
 *
 * Reference: Interactive_Plan.md §VIII·2, §VIII·4a
 *
 * Computes a composite score for each cluster node based on:
 *   - Available VRAM (higher = better)
 *   - GPU temperature (lower = better)
 *   - Network latency (lower = better, via InferenceQueueDepth as proxy)
 *   - Current layer load (fewer = better)
 *   - RDMA availability (bonus multiplier)
 *
 * Used by pick_best_node() for shard placement and KV eviction targeting.
 *--*/

#include <math.h>
#include <string.h>
#include <stdio.h>
#include "openmosix_tensor.h"

// ── Scoring Weights ─────────────────────────────────────────────────────────
// Tuned for datacenter F32 inference workloads
#define W_VRAM      0.40f    // VRAM availability is the primary constraint
#define W_TEMP      0.15f    // Thermal headroom
#define W_QUEUE     0.20f    // Inference queue depth (lower = better)
#define W_LOAD      0.20f    // Current layer count
#define W_RDMA      0.05f    // RDMA bonus (binary: 0 or 1)

// ── node_score ──────────────────────────────────────────────────────────────
//
// Returns a composite score in [0.0, 1.0] where higher = better candidate
// for receiving new tensor shards.
//
float node_score(HIVE_NODE* node)
{
    if (!node || !node->Active) return 0.0f;

    // Normalize VRAM: free/total ratio (VramTotalBytes → GB for comparison)
    float vram_total_gb = (float)node->VramTotalBytes / (1024.0f * 1024.0f * 1024.0f);
    float vram_ratio   = (vram_total_gb > 0.0f)
                       ? node->VramFreeGb / vram_total_gb
                       : 0.0f;

    // Normalize temperature: lower is better
    float temp_ratio   = (node->GpuTempC > 0.0f)
                       ? fminf(node->GpuTempC / 100.0f, 1.0f)
                       : 0.0f;

    // Normalize queue depth: lower is better (cap at 128 jobs)
    float queue_ratio  = fminf((float)node->InferenceQueueDepth / 128.0f, 1.0f);

    // Layer load: fraction of total layers currently assigned
    int assigned_layers = (node->LayerEnd >= node->LayerStart && node->LayerStart > 0)
                        ? (int)(node->LayerEnd - node->LayerStart + 1)
                        : 0;
    // Use global model config for total layers if available
    extern MODEL_CONFIG g_ModelConfig;
    float total_layers = (float)(g_ModelConfig.total_layers > 0
                        ? g_ModelConfig.total_layers : 80);
    float load_ratio   = (float)assigned_layers / total_layers;

    float rdma_bonus   = node->RdmaCapable ? 1.0f : 0.0f;

    float score = W_VRAM    * vram_ratio
                + W_TEMP    * (1.0f - temp_ratio)
                + W_QUEUE   * (1.0f - queue_ratio)
                + W_LOAD    * (1.0f - load_ratio)
                + W_RDMA    * rdma_bonus;

    return fmaxf(0.0f, fminf(1.0f, score));
}

// ── pick_best_node ──────────────────────────────────────────────────────────
//
// Scans the global node registry and returns the node with the highest score
// that has at least min_vram_gb free VRAM.
//
// Returns NULL if no eligible node found.
//
HIVE_NODE* pick_best_node(float min_vram_gb)
{
    HIVE_NODE* best = NULL;
    float best_score = -1.0f;

    for (int i = 0; i < g_NodeCount; i++) {
        HIVE_NODE* n = &g_NodeRegistry[i];
        if (!n->Active) continue;
        if (n->VramFreeGb < min_vram_gb) continue;

        float s = node_score(n);
        if (s > best_score) {
            best_score = s;
            best = n;
        }
    }

    return best;
}

// ── hive_mind_rebalance ─────────────────────────────────────────────────────
//
// Triggered on NODE_JOIN / NODE_LEAVE events.
// Delegates to rebalance_harmonic.c for Mark 1 harmonic rebalancing.
//
// Emits SHARD_ASSIGN messages to #hive-mind with vram_util targets.
//
void hive_mind_rebalance(void)
{
    // Delegate to Mark 1 harmonic rebalancer (§VIII·4a)
    extern void rebalance_harmonic_run(void);
    rebalance_harmonic_run();
}
