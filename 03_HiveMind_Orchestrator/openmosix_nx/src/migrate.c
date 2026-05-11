/*++
 * migrate.c — Complete Migration Cycle Orchestrator
 *
 * HIVE-MOSIX-001: CRIU checkpoint + cudaMemcpy VRAM + RDMA migration
 *
 * Reference:
 *   - Interactive_Plan.md §VIII·3 (lines 3492-3553) — CRIUgpu Protocol
 *   - Interactive_Plan.md §VIII·1 (lines 3366-3434) — Node Discovery
 *   - Interactive_Plan.md §VIII·2 (lines 3438-3488) — Load Balancing
 *
 * Architecture:
 *   This is the top-level migration controller. It monitors the cluster
 *   for thermal throttling, node failures, and manual eviction requests.
 *   When migration is triggered, it orchestrates the full CRIU+GPU+RDMA
 *   pipeline using the lower-level modules (criugpu_daemon, rdma_pool,
 *   rdma_stream).
 *
 *   Triggers:
 *     - GPU temp > 88°C → thermal migration
 *     - NODE_LEAVE → emergency redistribution
 *     - SHARD_MIGRATE command from hive_mind
 *     - Periodic rebalance after NODE_JOIN
 *
 * Acceptance criteria:
 *   Full process migration completes across network
 *--*/

#include "openmosix_tensor.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>     // PRIu64 for VramTotalBytes logging

// ── Global state (defined here, externed in header) ─────────────────────────
HIVE_NODE    g_NodeRegistry[MAX_NODES];
int          g_NodeCount = 0;
int          g_IrcFd = -1;
MODEL_CONFIG g_ModelConfig = {0};

// ── Thermal migration threshold ─────────────────────────────────────────────
#define THERMAL_MIGRATE_THRESHOLD_C     88.0f   // §VIII·3 line 3498

// ═══════════════════════════════════════════════════════════════════════════
// count_active_nodes — Count nodes with Active=1
// ═══════════════════════════════════════════════════════════════════════════

int count_active_nodes(void)
{
    int count = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (g_NodeRegistry[i].Active) count++;
    }
    return count;
}

// ═══════════════════════════════════════════════════════════════════════════
// find_or_alloc_node — Find existing node or allocate new slot
// ═══════════════════════════════════════════════════════════════════════════

int find_or_alloc_node(const char* nodeId)
{
    // Search for existing
    for (int i = 0; i < MAX_NODES; i++) {
        if (strncmp(g_NodeRegistry[i].NodeId, nodeId, NODE_ID_LEN - 1) == 0) {
            return i;
        }
    }
    // Find empty slot
    for (int i = 0; i < MAX_NODES; i++) {
        if (!g_NodeRegistry[i].Active &&
            g_NodeRegistry[i].NodeId[0] == '\0') {
            return i;
        }
    }
    return -1;  // Registry full
}

// ═══════════════════════════════════════════════════════════════════════════
// node_score — Score a node for shard placement
//
// Reference: §VIII·2 lines 3444-3461
// Returns 0-100+. Higher = better candidate.
// ═══════════════════════════════════════════════════════════════════════════

float node_score(HIVE_NODE* node)
{
    if (!node || !node->Active) return 0.0f;

    if (time(NULL) - node->LastHeartbeat > HEARTBEAT_TIMEOUT_S) {
        node->Active = 0;
        return 0.0f;
    }

    float vram_score    = node->VramFreeGb * 10.0f;
    float thermal_score = (90.0f - node->GpuTempC) * 1.5f;
    float queue_score   = 50.0f - (node->InferenceQueueDepth * 5.0f);

    float total = vram_score + thermal_score + queue_score;
    return (total < 0.0f) ? 0.0f : total;
}

// ═══════════════════════════════════════════════════════════════════════════
// pick_best_node — Find optimal target for shard placement
//
// Reference: §VIII·2 lines 3463-3475
// ═══════════════════════════════════════════════════════════════════════════

HIVE_NODE* pick_best_node(float min_vram_gb)
{
    float best_score = -1.0f;
    HIVE_NODE* best  = NULL;

    for (int i = 0; i < MAX_NODES; i++) {
        if (!g_NodeRegistry[i].Active) continue;
        if (g_NodeRegistry[i].VramFreeGb < min_vram_gb) continue;

        float s = node_score(&g_NodeRegistry[i]);
        if (s > best_score) {
            best_score = s;
            best = &g_NodeRegistry[i];
        }
    }
    return best;
}

// ═══════════════════════════════════════════════════════════════════════════
// handle_cluster_announce — Process NODE_JOIN from #cluster-announce
//
// Reference: §VIII·1 lines 3424-3433
// ═══════════════════════════════════════════════════════════════════════════

void handle_cluster_announce(const char* json_caps)
{
    if (!json_caps) return;

    HIVE_NODE node = {0};

    char node_id[NODE_ID_LEN] = {0};
    char ip_addr[46] = {0};
    float vram_gb = 0, vram_free = 0, gpu_temp = 0, ram_free = 0;
    int cpu_cores = 0, rdma = 0;

    const char* p = strstr(json_caps, "\"node_id\"");
    if (p) sscanf(p, "\"node_id\": \"%16[^\"]\"", node_id);

    p = strstr(json_caps, "\"ip_addr\"");
    if (p) sscanf(p, "\"ip_addr\": \"%45[^\"]\"", ip_addr);

    p = strstr(json_caps, "\"vram_gb\"");
    if (p) sscanf(p, "\"vram_gb\": %f", &vram_gb);

    p = strstr(json_caps, "\"vram_free\"");
    if (p) sscanf(p, "\"vram_free\": %f", &vram_free);

    p = strstr(json_caps, "\"gpu_temp_c\"");
    if (p) sscanf(p, "\"gpu_temp_c\": %f", &gpu_temp);

    p = strstr(json_caps, "\"cpu_cores\"");
    if (p) sscanf(p, "\"cpu_cores\": %d", &cpu_cores);

    p = strstr(json_caps, "\"ram_free_gb\"");
    if (p) sscanf(p, "\"ram_free_gb\": %f", &ram_free);

    p = strstr(json_caps, "\"rdma_capable\"");
    if (p) {
        if (strstr(p, "true")) rdma = 1;
    }

    strncpy(node.NodeId, node_id, NODE_ID_LEN - 1);
    strncpy(node.IpAddr, ip_addr, sizeof(node.IpAddr) - 1);
    node.VramFreeGb    = vram_free;
    node.VramTotalBytes = (uint64_t)(vram_gb * 1024) * 1024 * 1024;
    node.GpuTempC      = gpu_temp;
    node.CpuCores      = cpu_cores;
    node.RamFreeGb     = ram_free;
    node.RdmaCapable   = rdma;
    node.Active        = 1;
    node.LastHeartbeat = time(NULL);

    int slot = find_or_alloc_node(node.NodeId);
    if (slot >= 0) {
        g_NodeRegistry[slot] = node;
        g_NodeCount = count_active_nodes();

        fprintf(stderr, "[MIGRATE] NODE_JOIN: %s ip=%s (VRAM=%.0fGB/%"PRIu64"B, "
                "temp=%.0f°C, RDMA=%s) — total nodes=%d\n",
                node.NodeId, node.IpAddr[0] ? node.IpAddr : "unknown",
                vram_gb, node.VramTotalBytes, gpu_temp,
                rdma ? "yes" : "no", g_NodeCount);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// handle_node_pong — Update node stats from heartbeat
// ═══════════════════════════════════════════════════════════════════════════

void handle_node_pong(const char* node_id, float temp, float vram_free,
                       uint32_t queue_depth)
{
    for (int i = 0; i < MAX_NODES; i++) {
        if (!g_NodeRegistry[i].Active) continue;
        if (strncmp(g_NodeRegistry[i].NodeId, node_id, NODE_ID_LEN - 1) != 0)
            continue;

        g_NodeRegistry[i].GpuTempC = temp;
        g_NodeRegistry[i].VramFreeGb = vram_free;
        g_NodeRegistry[i].InferenceQueueDepth = queue_depth;
        g_NodeRegistry[i].LastHeartbeat = time(NULL);
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// prune_dead_nodes — Remove nodes that missed heartbeats
// ═══════════════════════════════════════════════════════════════════════════

void prune_dead_nodes(void)
{
    time_t now = time(NULL);

    for (int i = 0; i < MAX_NODES; i++) {
        if (!g_NodeRegistry[i].Active) continue;

        if (now - g_NodeRegistry[i].LastHeartbeat > HEARTBEAT_TIMEOUT_S) {
            fprintf(stderr, "[MIGRATE] Node %s DEAD — no heartbeat for %lds\n",
                    g_NodeRegistry[i].NodeId,
                    (long)(now - g_NodeRegistry[i].LastHeartbeat));
            g_NodeRegistry[i].Active = 0;
            g_NodeCount = count_active_nodes();

            char msg[256];
            snprintf(msg, sizeof(msg),
                "PRIVMSG #cluster-announce :NODE_LEAVE node=%s "
                "reason=heartbeat_timeout\r\n",
                g_NodeRegistry[i].NodeId);
            irc_send(g_IrcFd, msg);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// hive_mind_rebalance — Naive even-split layer distribution
// ═══════════════════════════════════════════════════════════════════════════

void hive_mind_rebalance(void)
{
    int active_nodes = count_active_nodes();
    if (active_nodes == 0) return;

    int total_layers = g_ModelConfig.total_layers;
    int layers_per_node = total_layers / active_nodes;
    int remainder = total_layers % active_nodes;

    int layer_cursor = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (!g_NodeRegistry[i].Active) continue;

        int node_layers = layers_per_node + (remainder-- > 0 ? 1 : 0);
        g_NodeRegistry[i].LayerStart = layer_cursor;
        g_NodeRegistry[i].LayerEnd   = layer_cursor + node_layers - 1;
        layer_cursor += node_layers;

        char msg[256];
        snprintf(msg, sizeof(msg),
            "PRIVMSG #hive-mind :SHARD_ASSIGN node=%s layers=%u-%u "
            "quant=F32\r\n",
            g_NodeRegistry[i].NodeId,
            g_NodeRegistry[i].LayerStart,
            g_NodeRegistry[i].LayerEnd);
        irc_send(g_IrcFd, msg);
    }

    fprintf(stderr, "[MIGRATE] Rebalanced: %d layers across %d nodes "
            "(%d layers/node)\n",
            total_layers, active_nodes, layers_per_node);
}

// ═══════════════════════════════════════════════════════════════════════════
// check_thermal_migration — Monitor cluster for thermal throttling
//
// Called periodically from hive_mind event loop.
// If any node exceeds 88°C, triggers migration to best available node.
// ═══════════════════════════════════════════════════════════════════════════

void check_thermal_migration(int irc_fd)
{
    for (int i = 0; i < MAX_NODES; i++) {
        if (!g_NodeRegistry[i].Active) continue;

        if (g_NodeRegistry[i].GpuTempC > THERMAL_MIGRATE_THRESHOLD_C) {
            fprintf(stderr, "[MIGRATE] Thermal alert: node %s at %.1f°C "
                    "(threshold %.1f°C)\n",
                    g_NodeRegistry[i].NodeId,
                    g_NodeRegistry[i].GpuTempC,
                    THERMAL_MIGRATE_THRESHOLD_C);

            // Find coolest available node
            float needed_vram = g_NodeRegistry[i].VramFreeGb;
            HIVE_NODE* target = pick_best_node(needed_vram);

            if (target && target != &g_NodeRegistry[i]) {
                criugpu_migrate(irc_fd,
                    g_NodeRegistry[i].NodeId,
                    target->NodeId,
                    g_NodeRegistry[i].LayerStart,
                    g_NodeRegistry[i].LayerEnd);
            } else {
                fprintf(stderr, "[MIGRATE] No suitable target for thermal "
                        "migration of %s\n", g_NodeRegistry[i].NodeId);
            }
        }
    }
}
