/*++
 * rdma_stream.h — RDMA Transfer API
 *
 * HIVE-MOSIX-008/009: Multi-segment RDMA streaming for F32 tensor migration
 *
 * Reference: Interactive_Plan.md §VIII·5c, §XIV·6e
 *
 * Provides zero-copy RDMA transfer primitives for shards >2GB via
 * segmented IBV_WR_RDMA_WRITE. Used by scout dispatch (§VII·4) and
 * KV cache eviction (§VIII·5e).
 *
 * NOTE: Canonical function prototypes are in openmosix_tensor.h.
 * This header provides the RDMA_CONNECTION struct and internal
 * implementation details used by rdma_pool.c and rdma_stream.c.
 *--*/

#ifndef _RDMA_STREAM_H_
#define _RDMA_STREAM_H_

#include <stdint.h>
#include <stddef.h>

/* 
 * Forward-declare HIVE_NODE to avoid circular include.
 * Full definition is in openmosix_tensor.h (which also declares
 * the public API: rdma_pool_connect, rdma_stream_shard, etc.)
 */
struct _HIVE_NODE;

#ifdef __cplusplus
extern "C" {
#endif

// Maximum RDMA segment size (2GB - limited by MR registration)
#define RDMA_MAX_SEGMENT_SIZE   (2ULL * 1024 * 1024 * 1024)

// Retry policy
#define RDMA_RETRY_MAX          3
#define RDMA_TIMEOUT_MS         5000

// ── Internal RDMA types (used by rdma_pool.c / rdma_stream.c) ───────────────

// Extended connection handle — internal to the RDMA subsystem
typedef struct _RDMA_CONNECTION_INTERNAL {
    void*           CmId;               // rdma_cm_id*
    void*           Qp;                 // ibv_qp*
    void*           LocalMr;            // ibv_mr* — pre-registered 2GB region
    void*           RemoteMrAddr;       // Remote memory region base address
    uint32_t        RemoteRkey;         // Remote memory region rkey
    uint8_t         Active;             // 1 = connected, 0 = disconnected
    uint64_t        LastActivity;       // time(NULL) of last successful transfer
    char            NodeId[32];         // Target node identifier
} RDMA_CONNECTION_INTERNAL;

// ── Internal functions (NOT in openmosix_tensor.h public API) ───────────────

// Stream with explicit internal connection handle (used by rdma_stream.c)
int rdma_stream_shard_conn(RDMA_CONNECTION_INTERNAL* conn, void* data, size_t size);

// TCP fallback for nodes without RDMA hardware
// Sets SO_SNDBUF=128MB for YeAH BDP optimization
// Marks socket with irc_set_dscp(fd, MOD_TENSOR) for CAKE Bulk tin
int tcp_stream_shard(struct _HIVE_NODE* target, void* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _RDMA_STREAM_H_ */
