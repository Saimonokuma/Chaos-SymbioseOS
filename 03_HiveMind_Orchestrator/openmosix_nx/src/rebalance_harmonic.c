/*++
 * rebalance_harmonic.c — Mark 1 Harmonic Rebalance (D.E.M.H.X.)
 *
 * HIVE-MOSIX-012: Weight-proportional layer assignment targeting H≈0.35 (π/9)
 *                 VRAM utilization per node. Replaces naive even-split.
 *
 * Reference:
 *   - Interactive_Plan.md §VIII·4a (lines 3619-3709) — Mark 1 Harmonic
 *   - D.E.M.H.X_Magick_Hex_3.0.md §Mark 1 Attractor
 *
 * Architecture:
 *   Instead of distributing layers evenly (which overloads small-VRAM nodes
 *   and wastes capacity on large-VRAM nodes), the harmonic rebalance assigns
 *   layers proportional to each node's capacity, targeting 35% VRAM
 *   utilization — the D.E.M.H.X. "acoustic resonator" sweet spot.
 *
 *   Post-rebalance, computes the system-wide Resonance Deviation Index (RDI)
 *   and reports to #telemetry.
 *
 *   SHARD_ASSIGN messages include vram_util and mark1_target fields.
 *
 * Acceptance criteria:
 *   Post-rebalance VRAM utilization within 0.25-0.45 for all nodes;
 *   system RDI reported correctly;
 *   SHARD_ASSIGN messages include utilization fields
 *--*/

#include "openmosix_tensor.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

// ═══════════════════════════════════════════════════════════════════════════
// hive_mind_rebalance_harmonic — D.E.M.H.X.-inspired layer distribution
//
// Reference: §VIII·4a lines 3629-3705
//
// Phase 1: Compute each node's capacity weight (target ~35% VRAM usage)
// Phase 2: Assign layers proportional to capacity
// Phase 3: Compute system-wide RDI and report to #telemetry
// ═══════════════════════════════════════════════════════════════════════════

void hive_mind_rebalance_harmonic(void)
{
    int active_nodes = count_active_nodes();
    if (active_nodes == 0) return;

    int total_layers = g_ModelConfig.total_layers;
    float bytes_per_layer = g_ModelConfig.model_size_bytes /
                            (float)total_layers;

    // ── Phase 1: Compute capacity weights ───────────────────────────────
    // §VIII·4a lines 3637-3646
    // Target: each node holds layers consuming ~35% (π/9) of its VRAM
    float total_capacity = 0.0f;

    for (int i = 0; i < MAX_NODES; i++) {
        if (!g_NodeRegistry[i].Active) continue;

        // Node target bytes = VRAM × π/9 (§VIII·4a line 3642)
        // Cast uint64_t → float at computation time (Issue #1)
        float node_target_bytes = (float)g_NodeRegistry[i].VramTotalBytes *
                                   DEMHX_HARMONIC_H;
        float node_capacity = node_target_bytes / bytes_per_layer;

        // Temporarily store capacity in LoadScore field
        g_NodeRegistry[i].LoadScore = node_capacity;
        total_capacity += node_capacity;
    }

    if (total_capacity <= 0.0f) {
        fprintf(stderr, "[HARMONIC] WARNING: zero total capacity — "
                "falling back to naive rebalance\n");
        hive_mind_rebalance();
        return;
    }

    // ── Phase 2: Assign layers proportional to capacity ─────────────────
    // §VIII·4a lines 3648-3678
    int layer_cursor = 0;
    int assigned = 0;

    for (int i = 0; i < MAX_NODES; i++) {
        if (!g_NodeRegistry[i].Active) continue;
        assigned++;

        int node_layers;
        if (assigned == active_nodes) {
            // Last node gets remainder (§VIII·4a line 3657)
            node_layers = total_layers - layer_cursor;
        } else {
            float proportion = g_NodeRegistry[i].LoadScore / total_capacity;
            node_layers = (int)(total_layers * proportion + 0.5f);
            if (node_layers < 1) node_layers = 1;
        }

        g_NodeRegistry[i].LayerStart = layer_cursor;
        g_NodeRegistry[i].LayerEnd   = layer_cursor + node_layers - 1;
        layer_cursor += node_layers;

        // Compute post-rebalance VRAM utilization (§VIII·4a line 3669)
        float util = (node_layers * bytes_per_layer) /
                     (float)g_NodeRegistry[i].VramTotalBytes;

        // Send SHARD_ASSIGN with utilization fields (§VIII·4a lines 3672-3678)
        char msg[512];
        snprintf(msg, sizeof(msg),
            "PRIVMSG #hive-mind :SHARD_ASSIGN node=%s layers=%u-%u quant=F32 "
            "vram_util=%.3f mark1_target=0.349\r\n",
            g_NodeRegistry[i].NodeId,
            g_NodeRegistry[i].LayerStart, g_NodeRegistry[i].LayerEnd,
            util);
        irc_send(g_IrcFd, msg);

        fprintf(stderr, "[HARMONIC] Node %s: layers %u-%u (%d layers) — "
                "VRAM util=%.1f%% (target=34.9%%)\n",
                g_NodeRegistry[i].NodeId,
                g_NodeRegistry[i].LayerStart, g_NodeRegistry[i].LayerEnd,
                node_layers, util * 100.0f);
    }

    // ── Phase 3: Compute system-wide RDI ────────────────────────────────
    // §VIII·4a lines 3681-3701
    float rdi_sum = 0.0f;
    int n = 0;

    for (int i = 0; i < MAX_NODES; i++) {
        if (!g_NodeRegistry[i].Active) continue;

        int layers = g_NodeRegistry[i].LayerEnd -
                     g_NodeRegistry[i].LayerStart + 1;
        float util = (layers * bytes_per_layer) /
                     (float)g_NodeRegistry[i].VramTotalBytes;
        float deviation = fabsf(util - DEMHX_HARMONIC_H);
        rdi_sum += deviation;
        n++;
    }

    float system_rdi = (n > 0) ? (DEMHX_HARMONIC_H - rdi_sum / n) : 0.0f;
    int converged = (fabsf(system_rdi - DEMHX_HARMONIC_H) <
                     DEMHX_CONVERGENCE_TOL);

    // Report RDI to #telemetry (§VIII·4a lines 3694-3701)
    char rdi_msg[256];
    snprintf(rdi_msg, sizeof(rdi_msg),
        "PRIVMSG #telemetry :RDI_REPORT source=rebalance rdi=%.6f "
        "target=0.349066 nodes=%d converged=%s\r\n",
        system_rdi, n, converged ? "true" : "false");
    irc_send(g_IrcFd, rdi_msg);

    fprintf(stderr, "[HARMONIC] Rebalance complete: %d layers, %d nodes, "
            "RDI=%.6f (target=0.349066, converged=%s)\n",
            total_layers, n, system_rdi,
            converged ? "true" : "false");
}
