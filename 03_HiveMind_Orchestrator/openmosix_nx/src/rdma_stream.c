/*++
 * rdma_stream.c — Multi-Segment RDMA Streaming
 *
 * HIVE-MOSIX-009: rdma_stream_shard() for arbitrary-size F32 tensor writes.
 *                 Handles shards > 2GB via segmented IBV_WR_RDMA_WRITE.
 *
 * Reference:
 *   - Interactive_Plan.md §VIII·5c (lines 3896-3940) — Multi-segment RDMA
 *   - Interactive_Plan.md §VII·4 (lines 2684-2752) — Scout dispatch
 *   - Interactive_Plan.md §VIII·5e (lines 4039-4126) — KV eviction
 *
 * Architecture:
 *   Streams F32 tensor shards of arbitrary size across RDMA connections.
 *   Uses the pre-registered 2GB memory region from rdma_pool.c as a
 *   staging buffer. Shards larger than 2GB are sent in 2GB segments.
 *
 *   On failure, retries up to RDMA_RETRY_MAX times via pool reconnect.
 *   If all retries exhausted, announces RDMA_DEAD on #cluster-announce.
 *
 * Acceptance criteria:
 *   492GB F32 model streams across RDMA without error;
 *   CRC64 matches on target
 *--*/

#include "openmosix_tensor.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

// RDMA_CONN and find_rdma_conn() are now in openmosix_tensor.h (Issue #2 fix)

// ═══════════════════════════════════════════════════════════════════════════
// rdma_stream_shard — Stream arbitrary-size F32 tensor via RDMA
//
// Reference: §VIII·5c lines 3896-3940
//
// Segments data into 2GB chunks (matching the pre-registered MR size),
// copies each segment into the staging buffer, and posts an
// IBV_WR_RDMA_WRITE for each segment.
//
// On failure: retries RDMA_RETRY_MAX times, announces RDMA_DEAD if
// all retries exhausted.
// ═══════════════════════════════════════════════════════════════════════════

int rdma_stream_shard(HIVE_NODE* target, void* data, size_t size)
{
    if (!target || !data || size == 0) return -EINVAL;

    RDMA_CONN* conn = find_rdma_conn(target->NodeId);
    if (!conn || !conn->Active) {
        fprintf(stderr, "[RDMA_STREAM] No active connection to node %s\n",
                target->NodeId);
        return -ENOTCONN;
    }

    // ── Segment the transfer into 2GB chunks ────────────────────────────
    // §VIII·5c line 3903
    size_t segment_size = RDMA_MR_SIZE;  // 2GB
    size_t offset = 0;
    uint32_t segment_num = 0;

    fprintf(stderr, "[RDMA_STREAM] Streaming %zu bytes to node %s "
            "(%zu segments)\n",
            size, target->NodeId, (size + segment_size - 1) / segment_size);

    while (offset < size) {
        size_t chunk = (size - offset > segment_size)
                       ? segment_size : (size - offset);

        // Copy data into pre-registered staging buffer
        // §VIII·5c line 3908
        memcpy(conn->LocalBuf, (uint8_t*)data + offset, chunk);

        // Build RDMA write work request
        // §VIII·5c lines 3910-3918
        struct ibv_sge sge = {
            .addr   = (uint64_t)(uintptr_t)conn->LocalBuf,
            .length = (uint32_t)chunk,
            .lkey   = conn->LocalMr->lkey
        };

        struct ibv_send_wr wr = {0};
        wr.opcode   = IBV_WR_RDMA_WRITE;
        wr.sg_list  = &sge;
        wr.num_sge  = 1;
        wr.send_flags = IBV_SEND_SIGNALED;

        struct ibv_send_wr* bad_wr = NULL;

        // Post send — with failover retry logic
        // §VIII·5c lines 3921-3934
        int ret = ibv_post_send(conn->CmId->qp, &wr, &bad_wr);

        if (ret != 0) {
            fprintf(stderr, "[RDMA_STREAM] ibv_post_send failed at segment %u "
                    "— entering failover\n", segment_num);

            // Failover: retry up to RDMA_RETRY_MAX times
            int recovered = 0;
            for (int retry = 0; retry < RDMA_RETRY_MAX; retry++) {
                // Announce failover attempt
                char msg[256];
                snprintf(msg, sizeof(msg),
                    "PRIVMSG #cluster-announce :RDMA_FAILOVER node=%s "
                    "retry=%d path=ib0\r\n",
                    target->NodeId, retry + 1);
                irc_send(g_IrcFd, msg);

                rdma_pool_reconnect_by_id(target->NodeId);

                // Re-lookup connection after reconnect
                conn = find_rdma_conn(target->NodeId);
                if (!conn || !conn->Active) continue;

                // Re-register staging buffer with new connection
                sge.lkey = conn->LocalMr->lkey;
                sge.addr = (uint64_t)(uintptr_t)conn->LocalBuf;

                // Re-copy data (buffer may have changed)
                memcpy(conn->LocalBuf, (uint8_t*)data + offset, chunk);

                ret = ibv_post_send(conn->CmId->qp, &wr, &bad_wr);
                if (ret == 0) {
                    recovered = 1;
                    fprintf(stderr, "[RDMA_STREAM] Failover succeeded on "
                            "retry %d\n", retry + 1);
                    break;
                }
            }

            if (!recovered) {
                // All retries exhausted — mark connection dead
                if (conn) conn->Active = 0;

                // Announce RDMA_DEAD
                char dead_msg[256];
                snprintf(dead_msg, sizeof(dead_msg),
                    "PRIVMSG #cluster-announce :RDMA_DEAD node=%s "
                    "reason=exhausted_retries\r\n",
                    target->NodeId);
                irc_send(g_IrcFd, dead_msg);

                fprintf(stderr, "[RDMA_STREAM] FATAL: All retries exhausted "
                        "for node %s — marked RDMA_DEAD\n", target->NodeId);
                return -EIO;
            }
        }

        // Wait for RDMA completion
        struct ibv_wc wc;
        int poll_ret;
        do {
            poll_ret = ibv_poll_cq(conn->CmId->send_cq, 1, &wc);
        } while (poll_ret == 0);

        if (poll_ret < 0 || wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "[RDMA_STREAM] Completion error at segment %u: "
                    "status=%d\n", segment_num,
                    poll_ret < 0 ? -1 : wc.status);
            return -EIO;
        }

        offset += chunk;
        segment_num++;
    }

    conn->LastActivity = time(NULL);

    fprintf(stderr, "[RDMA_STREAM] Complete: %zu bytes in %u segments to %s\n",
            size, segment_num, target->NodeId);
    return 0;
}
