/*++
 * openmosix_tensor.h — Neo-OpenMosix 2026 Cluster Engine Header
 *
 * HIVE-MOSIX-004: HIVE_NODE struct, node_score(), pick_best_node(),
 *                 hive_mind_rebalance()
 *
 * Reference:
 *   - Interactive_Plan.md §VIII·1 (lines 3366-3434) — Node Discovery
 *   - Interactive_Plan.md §VIII·2 (lines 3438-3488) — Thermal Score
 *   - Interactive_Plan.md §VIII·4 (lines 3557-3618) — Tensor Migration
 *   - Interactive_Plan.md §VIII·4a (lines 3619-3709) — Harmonic Rebalance
 *   - Interactive_Plan.md §XIII·7 (lines 5321-5334) — Verification Gates
 *
 * Architecture:
 *   This header defines the core data structures and functions for the
 *   Neo-OpenMosix clustering engine. Every cluster node (including the host)
 *   is represented by a HIVE_NODE struct. The load balancer scores nodes by
 *   VRAM availability, thermal headroom, and queue depth to determine optimal
 *   shard placement.
 *
 * Constitutional constraint: F32 full precision — no quantization.
 *--*/

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

// ── Cluster limits ──────────────────────────────────────────────────────────
#define MAX_NODES               64      // Maximum cluster nodes (§VIII·1 line 3420)
#define NODE_ID_LEN             17      // 16 hex chars + null
#define HEARTBEAT_TIMEOUT_S     10      // Dead if no PONG in 10s (§VIII·2 line 3450)
#define HEARTBEAT_INTERVAL_S    5       // PING every 5s (§VIII·2 line 3480)
#define MISSED_PINGS_DEAD       2       // Mark inactive after 2 missed PINGs (§VIII·2 line 3487)

// ── RDMA constants ──────────────────────────────────────────────────────────
#define RDMA_PORT               7471    // Tensor migration port (§VIII·5c line 3859)
#define RDMA_RETRY_MAX          3       // Reconnect attempts before dead (§VIII·5c line 3860)
#define RDMA_MR_SIZE            (2ULL * 1024 * 1024 * 1024) // 2GB pre-registered region

// ── io_uring constants ──────────────────────────────────────────────────────
#define URING_DEPTH             256     // Concurrent I/O operations (§VIII·5a line 3726)
#define SHARD_ALIGN             4096    // O_DIRECT alignment for NVMe (§VIII·5a line 3727)

// ── Huge page constants ─────────────────────────────────────────────────────
#define HUGEPAGE_2M             (2ULL * 1024 * 1024)
#define HUGEPAGE_1G             (1ULL * 1024 * 1024 * 1024)

// ── D.E.M.H.X. Harmonic Constants ──────────────────────────────────────────
// §VIII·4a line 3622: H ≈ 0.35 (π/9) — Universal Harmonic Constant
#define DEMHX_HARMONIC_H        0.349066f   // π/9
#define DEMHX_CONVERGENCE_TOL   0.01f       // |RDI - π/9| < 0.01 = converged

// ═══════════════════════════════════════════════════════════════════════════
// HIVE_NODE — Per-node state in the cluster registry
//
// Reference: §VIII·1 (lines 3408-3418)
//
// Populated from NODE_JOIN JSON caps on #cluster-announce.
// Updated by NODE_PONG heartbeats.
// ═══════════════════════════════════════════════════════════════════════════

typedef struct _HIVE_NODE {
    char        NodeId[NODE_ID_LEN];    // SHA-256(machine-id)[:16]
    char        IpAddr[46];             // Node IP (INET6_ADDRSTRLEN) for RDMA reconnect
    float       VramFreeGb;             // Current free VRAM (updated by PONG)
    uint64_t    VramTotalBytes;         // Total VRAM in bytes — uint64_t for precision
                                        // (float only has 24-bit mantissa → lossy above 16MB)
    float       GpuTempC;              // Current GPU temperature °C
    uint32_t    InferenceQueueDepth;    // Pending inference jobs
    uint32_t    LayerStart;             // Assigned model layer range start
    uint32_t    LayerEnd;               // Assigned model layer range end
    uint8_t     RdmaCapable;            // 1 if libibverbs available
    uint8_t     Active;                 // 1 if node is alive and responding
    time_t      LastHeartbeat;          // Timestamp of last NODE_PONG
    float       LoadScore;              // Temporary: used by harmonic rebalance
    char        LlamaBackend[16];       // "CUDA", "Vulkan", "CPU"
    uint32_t    CpuCores;              // Remote node CPU count
    float       RamFreeGb;             // Remote node free RAM
} HIVE_NODE;

// ── Global node registry ────────────────────────────────────────────────────
extern HIVE_NODE    g_NodeRegistry[MAX_NODES];
extern int          g_NodeCount;
extern int          g_IrcFd;           // IRC socket fd for cluster messaging

// ── Model configuration (populated at startup) ─────────────────────────────
typedef struct _MODEL_CONFIG {
    uint32_t    total_layers;           // e.g. 126 for 950B MoE
    uint64_t    model_size_bytes;       // Total F32 model size
    uint32_t    n_heads;                // Attention heads
    uint32_t    head_dim;               // Head dimension
    char        precision[8];           // Always "F32" (constitutional)
} MODEL_CONFIG;

extern MODEL_CONFIG g_ModelConfig;

// ═══════════════════════════════════════════════════════════════════════════
// Scoring & Selection API
// ═══════════════════════════════════════════════════════════════════════════

// Score a node for shard placement (0-100+, higher = better)
// §VIII·2 lines 3444-3461
float node_score(HIVE_NODE* node);

// Pick the best node with at least min_vram_gb free
// §VIII·2 lines 3463-3475
HIVE_NODE* pick_best_node(float min_vram_gb);

// Count currently active nodes
int count_active_nodes(void);

// Find or allocate a slot in g_NodeRegistry
int find_or_alloc_node(const char* nodeId);

// ═══════════════════════════════════════════════════════════════════════════
// Rebalance API
// ═══════════════════════════════════════════════════════════════════════════

// Naive even-split rebalance (fallback for single-node)
// §VIII·4 lines 3573-3604
void hive_mind_rebalance(void);

// Mark 1 Harmonic Rebalance — D.E.M.H.X. weight-proportional distribution
// Targets H≈0.35 (π/9) VRAM utilization per node
// §VIII·4a lines 3629-3705
void hive_mind_rebalance_harmonic(void);

// ═══════════════════════════════════════════════════════════════════════════
// Node Registry API
// ═══════════════════════════════════════════════════════════════════════════

// Handle NODE_JOIN from #cluster-announce (JSON caps)
// §VIII·1 lines 3424-3433
void handle_cluster_announce(const char* json_caps);

// Handle NODE_PONG heartbeat — update node stats
void handle_node_pong(const char* node_id, float temp, float vram_free,
                       uint32_t queue_depth);

// Mark dead nodes (no heartbeat in HEARTBEAT_TIMEOUT_S)
// §VIII·2 line 3487: nodes missing 2 consecutive PINGs marked Active=0
void prune_dead_nodes(void);

// ═══════════════════════════════════════════════════════════════════════════
// KV Cache Shard (§VIII·5e — Infinite Context)
// ═══════════════════════════════════════════════════════════════════════════

typedef struct _KV_SHARD {
    uint32_t    LayerStart;
    uint32_t    LayerEnd;
    uint64_t    TokenCount;             // Current tokens in this shard
    uint64_t    MaxTokens;              // Capacity based on VRAM
    float*      KeyCache;               // F32 key tensors
    float*      ValueCache;             // F32 value tensors
    uint64_t    MemoryBytes;            // Total allocated bytes
    uint8_t     OnHugePages;            // 1 if MAP_HUGETLB
} KV_SHARD;

// ═══════════════════════════════════════════════════════════════════════════
// RDMA_CONN — Per-node RDMA connection state (shared by rdma_pool + rdma_stream)
//
// Centralised here to avoid struct duplication across translation units.
// Reference: §VIII·5c lines 3862-3869
// ═══════════════════════════════════════════════════════════════════════════

#ifdef __linux__
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#endif

typedef struct _RDMA_CONN {
    struct rdma_cm_id*  CmId;           // RDMA CM connection ID
    struct ibv_mr*      LocalMr;        // Pre-registered 2GB memory region
    void*               LocalBuf;       // Buffer backing the MR
    uint8_t             Active;         // 1 if connection is live
    uint8_t             PathCount;      // Multi-path: active IB port count
    char                NodeId[NODE_ID_LEN];
    time_t              LastActivity;   // Timestamp of last successful send
} RDMA_CONN;

// Look up active RDMA connection by node ID (rdma_pool.c)
RDMA_CONN* find_rdma_conn(const char* node_id);

// ═══════════════════════════════════════════════════════════════════════════
// Forward declarations — implemented in separate .c files
// ═══════════════════════════════════════════════════════════════════════════

// tensor_io.c (HIVE-MOSIX-006)
int  tensor_io_init(void);
int  tensor_async_load(int fd, void* aligned_buf, size_t size, off_t offset,
                        uint64_t user_data);
typedef void (*TensorCallback)(uint64_t shard_id, int result);
int  tensor_reap_completions(TensorCallback callback, int max_batch);

// tensor_alloc.c (HIVE-MOSIX-007)
void* tensor_alloc_huge(size_t size_bytes);
int   tensor_pin_memory(void* ptr, size_t size);
void  tensor_free(void* ptr, size_t size);

// rdma_pool.c (HIVE-MOSIX-008)
int  rdma_pool_connect(const char* node_id, const char* ip);
void rdma_pool_disconnect(const char* node_id);
int  rdma_pool_reconnect_by_id(const char* node_id);

// rdma_stream.c (HIVE-MOSIX-009)
int  rdma_stream_shard(HIVE_NODE* target, void* data, size_t size);

// kv_shard_mgr.c (HIVE-MOSIX-010)
void kv_shard_append_token(int irc_fd, KV_SHARD* local_shard,
                            uint64_t token_id, float* key_data,
                            float* value_data);
void kv_shard_evict_oldest(int irc_fd, KV_SHARD* shard, uint32_t evict_count);

// criugpu_daemon.c (HIVE-MOSIX-003) — Multi-vendor GPU migration
int  criugpu_detect_backend(void);
int  criugpu_migrate(int irc_fd, const char* src_node_id,
                     const char* dst_node_id, uint32_t layer_start,
                     uint32_t layer_end);
int  rdma_migrate_shard(const char* dst_node_id, void* checkpoint, size_t size);
int  criu_checkpoint_shard(const char* checkpoint_dir,
                            void* vram_ptr, size_t vram_size);
int  criu_restore_shard(const char* checkpoint_dir,
                         void* vram_ptr, size_t vram_size);

// rebalance_harmonic.c (HIVE-MOSIX-012)
// hive_mind_rebalance_harmonic() — declared above

// irc_send helper (from symbiose_ircd module)
extern int irc_send(int fd, const char* msg);
