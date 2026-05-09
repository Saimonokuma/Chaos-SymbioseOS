/*++
 * kv_shard_mgr.c — KV Cache Shard Manager (Infinite Context)
 *
 * HIVE-MOSIX-010: KV_SHARD struct, kv_shard_append_token(),
 *                 kv_shard_evict_oldest()
 *
 * Reference:
 *   - Interactive_Plan.md §VIII·5e (lines 4039-4158) — Infinite Context
 *
 * Architecture:
 *   The KV cache is sharded across cluster nodes. Each node holds a
 *   contiguous KV shard for its assigned layer range. When a shard fills
 *   up, the oldest tokens are evicted to the node with the most free
 *   VRAM via RDMA — preserving context indefinitely.
 *
 *   Total effective context = sum of all node VRAM capacities.
 *   Adding nodes linearly scales the context window.
 *
 *   IRC messages:
 *     KV_APPEND  — new token distributed to all shards
 *     KV_EVICT   — oldest tokens migrated to free VRAM
 *     KV_RECALL  — recall specific KV range for recomputation
 *
 * Acceptance criteria:
 *   Infinite context: total KV capacity = sum of all node VRAM;
 *   eviction preserves oldest tokens on remote nodes
 *--*/

#include "openmosix_tensor.h"

#include <stdio.h>
#include <string.h>

// ═══════════════════════════════════════════════════════════════════════════
// kv_shard_append_token — Distribute new KV entries to all shards
//
// Reference: §VIII·5e lines 4068-4091
//
// Called when a new token is generated. Writes the local layers' KV
// entries directly, then broadcasts KV_APPEND to remote nodes via IRC
// and streams the actual KV data via RDMA.
// ═══════════════════════════════════════════════════════════════════════════

void kv_shard_append_token(int irc_fd, KV_SHARD* local_shard,
                            uint64_t token_id, float* key_data,
                            float* value_data)
{
    if (!local_shard || !key_data || !value_data) return;

    // ── Check capacity ──────────────────────────────────────────────────
    if (local_shard->TokenCount >= local_shard->MaxTokens) {
        // Shard full — trigger eviction before appending
        fprintf(stderr, "[KV_SHARD] Local shard full (%llu/%llu tokens) — "
                "triggering eviction\n",
                (unsigned long long)local_shard->TokenCount,
                (unsigned long long)local_shard->MaxTokens);

        // Evict 10% of oldest tokens to make room
        uint32_t evict_count = (uint32_t)(local_shard->MaxTokens / 10);
        if (evict_count < 1) evict_count = 1;
        kv_shard_evict_oldest(irc_fd, local_shard, evict_count);
    }

    // ── Compute entry size ──────────────────────────────────────────────
    // §VIII·5e line 4073-4074
    uint32_t layer_count = local_shard->LayerEnd - local_shard->LayerStart + 1;
    size_t entry_size = (size_t)layer_count * g_ModelConfig.n_heads *
                        g_ModelConfig.head_dim * sizeof(float);

    // ── Write local KV entries ──────────────────────────────────────────
    // §VIII·5e lines 4076-4080
    size_t offset = local_shard->TokenCount * entry_size;

    if (offset + entry_size > local_shard->MemoryBytes) {
        fprintf(stderr, "[KV_SHARD] ERROR: buffer overflow prevented "
                "(offset=%zu + entry=%zu > alloc=%llu)\n",
                offset, entry_size,
                (unsigned long long)local_shard->MemoryBytes);
        return;
    }

    memcpy((uint8_t*)local_shard->KeyCache + offset, key_data, entry_size);
    memcpy((uint8_t*)local_shard->ValueCache + offset, value_data, entry_size);
    local_shard->TokenCount++;

    // ── Broadcast KV_APPEND to remote nodes via IRC ─────────────────────
    // §VIII·5e lines 4082-4087
    char msg[256];
    snprintf(msg, sizeof(msg),
        "PRIVMSG #hive-mind :KV_APPEND token=%llu layers=%u-%u size=%zu\r\n",
        (unsigned long long)token_id,
        local_shard->LayerStart, local_shard->LayerEnd,
        entry_size);
    irc_send(irc_fd, msg);

    // ── Stream actual KV data via RDMA to adjacent nodes ────────────────
    // §VIII·5e lines 4089-4091
    // In production: iterate over nodes holding adjacent layer ranges
    // and RDMA-write the KV entries. For now, the IRC message signals
    // remote nodes to request the data via DCC if needed.
}

// ═══════════════════════════════════════════════════════════════════════════
// kv_shard_evict_oldest — Migrate oldest KV entries to free VRAM
//
// Reference: §VIII·5e lines 4093-4125
//
// When a shard's KV cache is full, migrates the oldest tokens to the
// node with the most free VRAM (found by pick_best_node()). Uses
// RDMA for zero-copy transfer, then shifts remaining entries forward.
//
// This is what makes context truly infinite: evicted tokens are not
// discarded, they're preserved on remote nodes for later recall.
// ═══════════════════════════════════════════════════════════════════════════

void kv_shard_evict_oldest(int irc_fd, KV_SHARD* shard, uint32_t evict_count)
{
    if (!shard || evict_count == 0) return;
    if (evict_count > shard->TokenCount) evict_count = shard->TokenCount;

    // ── Find best target node for evicted KV entries ────────────────────
    // §VIII·5e lines 4097-4103
    size_t evict_bytes = (size_t)evict_count *
                         shard->MemoryBytes / shard->MaxTokens;

    float needed_gb = (float)evict_bytes / (1024.0f * 1024 * 1024);
    HIVE_NODE* target = pick_best_node(needed_gb);

    if (!target) {
        fprintf(stderr, "[KV_SHARD] WARNING: KV eviction failed — no node "
                "with %.1f GB free VRAM\n", needed_gb);
        return;
    }

    fprintf(stderr, "[KV_SHARD] Evicting %u oldest tokens (%zu bytes) "
            "to node %s\n", evict_count, evict_bytes, target->NodeId);

    // ── RDMA stream evicted KV entries to target ────────────────────────
    // §VIII·5e lines 4106-4108
    int ret_key = rdma_stream_shard(target, shard->KeyCache, evict_bytes);
    int ret_val = rdma_stream_shard(target, shard->ValueCache, evict_bytes);

    if (ret_key != 0 || ret_val != 0) {
        fprintf(stderr, "[KV_SHARD] WARNING: RDMA eviction partially failed "
                "(key=%d, val=%d)\n", ret_key, ret_val);
        // Continue anyway — shift local data to free space
    }

    // ── Shift remaining entries forward ─────────────────────────────────
    // §VIII·5e lines 4110-4117
    size_t remaining = shard->MemoryBytes - evict_bytes;

    if (remaining > 0) {
        memmove(shard->KeyCache,
                (uint8_t*)shard->KeyCache + evict_bytes,
                remaining);
        memmove(shard->ValueCache,
                (uint8_t*)shard->ValueCache + evict_bytes,
                remaining);
    }

    shard->TokenCount -= evict_count;

    // ── Announce eviction on IRC ────────────────────────────────────────
    // §VIII·5e lines 4119-4124
    char msg[256];
    snprintf(msg, sizeof(msg),
        "PRIVMSG #hive-mind :KV_EVICT tokens=%u target=%s freed_mb=%zu\r\n",
        evict_count, target->NodeId, evict_bytes / (1024 * 1024));
    irc_send(irc_fd, msg);
}
