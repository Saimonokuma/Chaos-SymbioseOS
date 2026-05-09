/*++
 * rdma_pool.c — RDMA Connection Pool + Multi-Path Failover
 *
 * HIVE-MOSIX-008: Pre-registered 2GB memory regions per node,
 *                 rdma_pool_connect(), multi-path failover with
 *                 RDMA_RETRY_MAX=3
 *
 * Reference:
 *   - Interactive_Plan.md §VIII·5c (lines 3851-3948) — RDMA Pool
 *
 * Architecture:
 *   Maintains pre-established RDMA connections to all active cluster nodes.
 *   Each connection has a 2GB pre-registered memory region for zero-copy
 *   tensor sends. On link failure, automatic failover with up to 3 retries.
 *
 *   IRC integration:
 *     RDMA_POOL_READY — pool connected to new node
 *     RDMA_FAILOVER   — link failure, reconnecting
 *     RDMA_DEAD       — all retries exhausted, node unreachable
 *
 * Acceptance criteria:
 *   Pool pre-connects on NODE_JOIN; failover completes within 3 retries
 *--*/

#include "openmosix_tensor.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <rdma/rdma_verbs.h>   /* rdma_reg_msgs, rdma_dereg_mr, rdma_create_ep */

// RDMA_CONN is now defined in openmosix_tensor.h (Issue #2 fix — no more duplication)

// ── Connection pool ─────────────────────────────────────────────────────────
#define RDMA_POOL_SIZE  MAX_NODES       // One connection per node (§VIII·5c line 3858)
static RDMA_CONN g_RdmaPool[RDMA_POOL_SIZE];

// ── Internal helpers ────────────────────────────────────────────────────────

static int find_rdma_slot(const char* node_id)
{
    // Find existing connection for this node
    for (int i = 0; i < RDMA_POOL_SIZE; i++) {
        if (g_RdmaPool[i].Active &&
            strncmp(g_RdmaPool[i].NodeId, node_id, NODE_ID_LEN - 1) == 0) {
            return i;
        }
    }
    // Find first empty slot
    for (int i = 0; i < RDMA_POOL_SIZE; i++) {
        if (!g_RdmaPool[i].Active && g_RdmaPool[i].CmId == NULL) {
            return i;
        }
    }
    return -1;  // Pool full
}

// ═══════════════════════════════════════════════════════════════════════════
// rdma_pool_connect — Pre-connect to a cluster node
//
// Reference: §VIII·5c lines 3873-3893
//
// Called on NODE_JOIN events. Establishes RDMA connection and registers
// a 2GB memory region for zero-copy tensor sends.
// Announces RDMA_POOL_READY on #cluster-announce.
// ═══════════════════════════════════════════════════════════════════════════

int rdma_pool_connect(const char* node_id, const char* ip)
{
    int slot = find_rdma_slot(node_id);
    if (slot < 0) {
        fprintf(stderr, "[RDMA_POOL] ERROR: pool full, cannot connect to %s\n",
                node_id);
        return -1;
    }

    RDMA_CONN* conn = &g_RdmaPool[slot];

    // ── Resolve RDMA address ────────────────────────────────────────────
    struct rdma_addrinfo hints = {0};
    hints.ai_port_space = RDMA_PS_TCP;

    struct rdma_addrinfo* res;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", RDMA_PORT);

    if (rdma_getaddrinfo(ip, port_str, &hints, &res) != 0) {
        fprintf(stderr, "[RDMA_POOL] getaddrinfo failed for %s:%d: %s\n",
                ip, RDMA_PORT, strerror(errno));
        return -1;
    }

    // ── Create endpoint and connect ─────────────────────────────────────
    if (rdma_create_ep(&conn->CmId, res, NULL, NULL) != 0) {
        fprintf(stderr, "[RDMA_POOL] create_ep failed for %s\n", ip);
        rdma_freeaddrinfo(res);
        return -1;
    }
    rdma_freeaddrinfo(res);

    if (rdma_connect(conn->CmId, NULL) != 0) {
        fprintf(stderr, "[RDMA_POOL] connect failed for %s: %s\n",
                ip, strerror(errno));
        rdma_destroy_ep(conn->CmId);
        conn->CmId = NULL;
        return -1;
    }

    // ── Pre-register 2GB memory region for zero-copy sends ──────────────
    // §VIII·5c line 3887-3888
    conn->LocalBuf = tensor_alloc_huge(RDMA_MR_SIZE);
    if (!conn->LocalBuf) {
        fprintf(stderr, "[RDMA_POOL] Failed to allocate 2GB MR buffer\n");
        rdma_disconnect(conn->CmId);
        rdma_destroy_ep(conn->CmId);
        conn->CmId = NULL;
        return -1;
    }

    conn->LocalMr = rdma_reg_msgs(conn->CmId, conn->LocalBuf, RDMA_MR_SIZE);
    if (!conn->LocalMr) {
        fprintf(stderr, "[RDMA_POOL] rdma_reg_msgs failed: %s\n",
                strerror(errno));
        tensor_free(conn->LocalBuf, RDMA_MR_SIZE);
        conn->LocalBuf = NULL;
        rdma_disconnect(conn->CmId);
        rdma_destroy_ep(conn->CmId);
        conn->CmId = NULL;
        return -1;
    }

    // ── Mark connection active ──────────────────────────────────────────
    strncpy(conn->NodeId, node_id, NODE_ID_LEN - 1);
    conn->NodeId[NODE_ID_LEN - 1] = '\0';
    conn->Active = 1;
    conn->PathCount = 1;
    conn->LastActivity = time(NULL);

    // ── Announce on #cluster-announce ────────────────────────────────────
    // §VIII·5c line 3945
    char msg[256];
    snprintf(msg, sizeof(msg),
        "PRIVMSG #cluster-announce :RDMA_POOL_READY node=%s paths=%u "
        "mr_size=2GB\r\n",
        node_id, conn->PathCount);
    irc_send(g_IrcFd, msg);

    fprintf(stderr, "[RDMA_POOL] Connected to %s (%s:%d) — 2GB MR registered\n",
            node_id, ip, RDMA_PORT);
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// rdma_pool_disconnect — Tear down connection to a node
// ═══════════════════════════════════════════════════════════════════════════

void rdma_pool_disconnect(const char* node_id)
{
    for (int i = 0; i < RDMA_POOL_SIZE; i++) {
        if (!g_RdmaPool[i].Active) continue;
        if (strncmp(g_RdmaPool[i].NodeId, node_id, NODE_ID_LEN - 1) != 0) continue;

        RDMA_CONN* conn = &g_RdmaPool[i];

        if (conn->LocalMr) {
            rdma_dereg_mr(conn->LocalMr);
            conn->LocalMr = NULL;
        }
        if (conn->LocalBuf) {
            tensor_free(conn->LocalBuf, RDMA_MR_SIZE);
            conn->LocalBuf = NULL;
        }
        if (conn->CmId) {
            rdma_disconnect(conn->CmId);
            rdma_destroy_ep(conn->CmId);
            conn->CmId = NULL;
        }

        conn->Active = 0;
        fprintf(stderr, "[RDMA_POOL] Disconnected from %s\n", node_id);
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// rdma_pool_reconnect_by_id — Reconnect after link failure
//
// Used by rdma_stream_shard() during failover.
// Announces RDMA_FAILOVER on #cluster-announce.
// ═══════════════════════════════════════════════════════════════════════════

int rdma_pool_reconnect_by_id(const char* node_id)
{
    for (int i = 0; i < RDMA_POOL_SIZE; i++) {
        if (strncmp(g_RdmaPool[i].NodeId, node_id, NODE_ID_LEN - 1) != 0)
            continue;

        RDMA_CONN* conn = &g_RdmaPool[i];

        // Tear down old connection cleanly
        if (conn->LocalMr) {
            rdma_dereg_mr(conn->LocalMr);
            conn->LocalMr = NULL;
        }
        if (conn->LocalBuf) {
            tensor_free(conn->LocalBuf, RDMA_MR_SIZE);
            conn->LocalBuf = NULL;
        }
        if (conn->CmId) {
            rdma_disconnect(conn->CmId);
            rdma_destroy_ep(conn->CmId);
            conn->CmId = NULL;
        }
        conn->Active = 0;

        // Announce failover attempt
        char msg[256];
        snprintf(msg, sizeof(msg),
            "PRIVMSG #cluster-announce :RDMA_FAILOVER node=%s retry=1\r\n",
            node_id);
        irc_send(g_IrcFd, msg);

        fprintf(stderr, "[RDMA_POOL] Attempting reconnect to %s\n", node_id);

        // Look up IP from node registry and actually reconnect
        // (Issue #3 fix — was previously a no-op that returned 0)
        for (int j = 0; j < MAX_NODES; j++) {
            if (strncmp(g_NodeRegistry[j].NodeId, node_id,
                        NODE_ID_LEN - 1) == 0 && g_NodeRegistry[j].Active) {

                if (g_NodeRegistry[j].IpAddr[0] == '\0') {
                    fprintf(stderr, "[RDMA_POOL] No IP address cached for "
                            "node %s — cannot reconnect\n", node_id);
                    return -1;
                }

                // Re-establish connection via the standard connect path
                int ret = rdma_pool_connect(node_id, g_NodeRegistry[j].IpAddr);
                if (ret == 0) {
                    fprintf(stderr, "[RDMA_POOL] Reconnect to %s (%s) "
                            "succeeded\n", node_id, g_NodeRegistry[j].IpAddr);
                }
                return ret;
            }
        }

        fprintf(stderr, "[RDMA_POOL] Node %s not found in registry — "
                "cannot reconnect\n", node_id);
        return -1;
    }
    return -1;  // Node ID not in pool
}

// ═══════════════════════════════════════════════════════════════════════════
// find_rdma_conn — Look up active RDMA connection by node ID
//
// Used internally by rdma_stream_shard() in rdma_stream.c
// ═══════════════════════════════════════════════════════════════════════════

RDMA_CONN* find_rdma_conn(const char* node_id)
{
    for (int i = 0; i < RDMA_POOL_SIZE; i++) {
        if (g_RdmaPool[i].Active &&
            strncmp(g_RdmaPool[i].NodeId, node_id, NODE_ID_LEN - 1) == 0) {
            return &g_RdmaPool[i];
        }
    }
    return NULL;
}
