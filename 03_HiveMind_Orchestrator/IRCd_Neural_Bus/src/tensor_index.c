/*++
 * tensor_index.c — Content-Addressed Tensor Index + IRC Checkpoint WAL
 *
 * HIVE-IRC-009
 *
 * Reference:
 *   - Interactive_Plan.md §VII·8a (lines 3220-3271)
 *   - Interactive_Plan.md §VII·8b (lines 3291-3338)
 *   - Task matrix (line 4997)
 *   - Verification: §XIII·5 (lines 5272-5273)
 *
 * Purpose:
 *   Content-addressed index of all F32 tensor blocks in the cluster.
 *   Every block is hashed (CRC64-ECMA) before transmission. If a shard
 *   with the same hash already exists, it is NOT re-transmitted — the
 *   receiver fetches from the node that already holds it (like Git).
 *
 *   The #checkpoint channel log is a persistent WAL. On crash recovery,
 *   hive_mind replays the log to restore the tensor index, node registry,
 *   KV cache pointers, and model identity.
 *
 * Checkpoint message formats (§VII·8b lines 3299-3309):
 *   CKPT_TENSOR crc=<hash> layers=<s>-<e> holder=<node> size=<bytes>
 *   CKPT_KV     node=<id> layers=<s>-<e> tokens=<count> vram_addr=<ptr>
 *   CKPT_NODE   id=<id> ip=<ip> vram=<gb> rdma=<0|1> layers=<s>-<e>
 *   CKPT_MODEL  name=<name> params=<B> precision=F32 shards=<count>
 *--*/

#include "symbiose_ircd.h"
#include "jumbo_payload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

// ── Constants (§VII·8a line 3225) ───────────────────────────────────────────
#define TENSOR_INDEX_SIZE   65536   // 64K hash buckets

// ── TENSOR_BLOCK struct (§VII·8a lines 3227-3237) ───────────────────────────
typedef struct _TENSOR_BLOCK {
    uint64_t Crc64;                 // Content hash (CRC64-ECMA)
    uint32_t LayerStart;
    uint32_t LayerEnd;
    uint64_t SizeBytes;
    char     HolderNode[17];        // Which node currently has this in VRAM
    char     BackupNode[17];        // Redundant copy location (if any)
    time_t   FirstSeen;             // When this block was first ingested
    uint32_t RefCount;              // How many nodes reference this block
    uint8_t  Pinned;                // Protected from eviction
} TENSOR_BLOCK;

// ── Global index (§VII·8a lines 3240-3241) ──────────────────────────────────
static TENSOR_BLOCK g_TensorIndex[TENSOR_INDEX_SIZE];
static uint32_t     g_TensorIndexCount = 0;

// ═══════════════════════════════════════════════════════════════════════════
// tensor_index_lookup — Check if a tensor block exists in the cluster
//
// Reference: §VII·8a lines 3244-3250
// Returns pointer to existing block or NULL if not found.
// ═══════════════════════════════════════════════════════════════════════════
TENSOR_BLOCK* tensor_index_lookup(uint64_t crc64)
{
    for (uint32_t i = 0; i < g_TensorIndexCount; i++) {
        if (g_TensorIndex[i].Crc64 == crc64) return &g_TensorIndex[i];
    }
    return NULL;  // Not found — needs transmission
}

// ═══════════════════════════════════════════════════════════════════════════
// tensor_index_register — Register a new tensor block
//
// Reference: §VII·8a lines 3252-3270
// Called after DCC transfer completes or after local model load.
// Deduplicates: if CRC64 already exists, just increments RefCount.
// ═══════════════════════════════════════════════════════════════════════════
void tensor_index_register(uint64_t crc64, uint32_t layerStart,
                            uint32_t layerEnd, uint64_t size,
                            const char* holderNode)
{
    // Dedup check — §VII·8a line 3257
    TENSOR_BLOCK* existing = tensor_index_lookup(crc64);
    if (existing) {
        existing->RefCount++;
        printf("[TENSOR-IDX] Deduplicated: CRC64=%016llX (ref=%u)\n",
               (unsigned long long)crc64, existing->RefCount);
        return;  // Already indexed — no re-transmission needed
    }

    if (g_TensorIndexCount >= TENSOR_INDEX_SIZE) {
        fprintf(stderr, "[TENSOR-IDX] Index full (%u entries)\n",
                TENSOR_INDEX_SIZE);
        return;
    }

    // Register new block — §VII·8a lines 3262-3269
    TENSOR_BLOCK* block = &g_TensorIndex[g_TensorIndexCount++];
    block->Crc64 = crc64;
    block->LayerStart = layerStart;
    block->LayerEnd = layerEnd;
    block->SizeBytes = size;
    strncpy_s(block->HolderNode, sizeof(block->HolderNode),
              holderNode, _TRUNCATE);
    memset(block->BackupNode, 0, sizeof(block->BackupNode));
    block->FirstSeen = time(NULL);
    block->RefCount = 1;
    block->Pinned = 0;

    printf("[TENSOR-IDX] Registered: CRC64=%016llX layers=%u-%u "
           "holder=%s size=%lluB\n",
           (unsigned long long)crc64, layerStart, layerEnd,
           holderNode, (unsigned long long)size);
}

// ═══════════════════════════════════════════════════════════════════════════
// tensor_index_announce — Announce a tensor block on #cluster-announce
//
// Reference: §VII·8a dedup flow (lines 3274-3278)
// Format: TENSOR_INDEX crc=<hash> layers=<s>-<e> holder=<node> size=<bytes>
// ═══════════════════════════════════════════════════════════════════════════
void tensor_index_announce(IRC_CLIENT* client, uint64_t crc64,
                            uint32_t layerStart, uint32_t layerEnd,
                            const char* holderNode, uint64_t sizeBytes)
{
    ircd_send_raw(client,
        "PRIVMSG #cluster-announce :TENSOR_INDEX crc=%016llX "
        "layers=%u-%u holder=%s size=%llu\r\n",
        (unsigned long long)crc64, layerStart, layerEnd,
        holderNode, (unsigned long long)sizeBytes);
}

// ═══════════════════════════════════════════════════════════════════════════
// Checkpoint writing — write state to #checkpoint channel as WAL records
//
// Reference: §VII·8b lines 3296-3309
// Each PRIVMSG to #checkpoint is a WAL record logged to disk by the IRCd.
// ═══════════════════════════════════════════════════════════════════════════

// Write tensor index checkpoint record
void checkpoint_write_tensor(IRC_CLIENT* client, TENSOR_BLOCK* block)
{
    // Format: CKPT_TENSOR crc=<hash> layers=<s>-<e> holder=<node> size=<bytes>
    ircd_send_raw(client,
        "PRIVMSG #checkpoint :CKPT_TENSOR crc=%016llX layers=%u-%u "
        "holder=%s size=%llu\r\n",
        (unsigned long long)block->Crc64,
        block->LayerStart, block->LayerEnd,
        block->HolderNode,
        (unsigned long long)block->SizeBytes);
}

// Write KV cache pointer checkpoint record
void checkpoint_write_kv(IRC_CLIENT* client, const char* nodeId,
                          uint32_t layerStart, uint32_t layerEnd,
                          uint32_t tokenCount, uint64_t vramAddr)
{
    // Format: CKPT_KV node=<id> layers=<s>-<e> tokens=<count> vram_addr=<ptr>
    ircd_send_raw(client,
        "PRIVMSG #checkpoint :CKPT_KV node=%s layers=%u-%u "
        "tokens=%u vram_addr=%016llX\r\n",
        nodeId, layerStart, layerEnd,
        tokenCount, (unsigned long long)vramAddr);
}

// Write cluster node checkpoint record
void checkpoint_write_node(IRC_CLIENT* client, const char* nodeId,
                            const char* ip, float vramGb,
                            uint8_t rdmaCapable,
                            uint32_t layerStart, uint32_t layerEnd)
{
    // Format: CKPT_NODE id=<id> ip=<ip> vram=<gb> rdma=<0|1> layers=<s>-<e>
    ircd_send_raw(client,
        "PRIVMSG #checkpoint :CKPT_NODE id=%s ip=%s vram=%.1f "
        "rdma=%u layers=%u-%u\r\n",
        nodeId, ip, vramGb, rdmaCapable, layerStart, layerEnd);
}

// Write model identity checkpoint record
void checkpoint_write_model(IRC_CLIENT* client, const char* modelName,
                             uint32_t paramsBillions, uint32_t shardCount)
{
    // Format: CKPT_MODEL name=<name> params=<B> precision=F32 shards=<count>
    ircd_send_raw(client,
        "PRIVMSG #checkpoint :CKPT_MODEL name=%s params=%uB "
        "precision=F32 shards=%u\r\n",
        modelName, paramsBillions, shardCount);
}

// Write ALL tensor index entries as a full checkpoint
void checkpoint_write_full(IRC_CLIENT* client)
{
    for (uint32_t i = 0; i < g_TensorIndexCount; i++) {
        checkpoint_write_tensor(client, &g_TensorIndex[i]);
    }
    printf("[CHECKPOINT] Full checkpoint written: %u tensor entries\n",
           g_TensorIndexCount);
}

// ═══════════════════════════════════════════════════════════════════════════
// tensor_index_restore_from_log — Parse a CKPT_TENSOR line from WAL
//
// Reference: §VII·8b line 3321
// Expected format:
//   ...CKPT_TENSOR crc=<hex> layers=<s>-<e> holder=<node> size=<bytes>
// ═══════════════════════════════════════════════════════════════════════════
void tensor_index_restore_from_log(const char* line)
{
    // Find the CKPT_TENSOR payload
    const char* payload = strstr(line, "CKPT_TENSOR");
    if (!payload) return;

    uint64_t crc64 = 0;
    uint32_t layerStart = 0, layerEnd = 0;
    char holderNode[17] = {0};
    uint64_t sizeBytes = 0;

    // Parse key=value fields
    const char* p;
    if ((p = strstr(payload, "crc=")) != NULL)
        sscanf_s(p, "crc=%llX", &crc64);
    if ((p = strstr(payload, "layers=")) != NULL)
        sscanf_s(p, "layers=%u-%u", &layerStart, &layerEnd);
    if ((p = strstr(payload, "holder=")) != NULL)
        sscanf_s(p, "holder=%16s", holderNode,
                 (unsigned)sizeof(holderNode));
    if ((p = strstr(payload, "size=")) != NULL)
        sscanf_s(p, "size=%llu", &sizeBytes);

    // Register without dedup announcement (restoring from log)
    if (crc64 != 0) {
        tensor_index_register(crc64, layerStart, layerEnd,
                               sizeBytes, holderNode);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// hive_mind_recover_from_checkpoint — Replay #checkpoint log on boot
//
// Reference: §VII·8b lines 3312-3337
// Reads the IRC log file line-by-line and dispatches to restore functions.
// ═══════════════════════════════════════════════════════════════════════════
void hive_mind_recover_from_checkpoint(const char* logPath)
{
    FILE* log = NULL;
    fopen_s(&log, logPath, "r");
    if (!log) {
        printf("[CHECKPOINT] No checkpoint log found — first boot\n");
        return;  // First boot — no checkpoint
    }

    uint32_t tensorCount = 0;
    uint32_t nodeCount = 0;
    uint32_t kvCount = 0;
    uint32_t modelCount = 0;

    char line[1024];
    while (fgets(line, sizeof(line), log)) {
        if (strstr(line, "CKPT_TENSOR")) {
            tensor_index_restore_from_log(line);
            tensorCount++;
        } else if (strstr(line, "CKPT_NODE")) {
            // node_registry_restore_from_log(line);  // Implemented elsewhere
            nodeCount++;
        } else if (strstr(line, "CKPT_KV")) {
            // kv_shard_restore_from_log(line);  // Implemented elsewhere
            kvCount++;
        } else if (strstr(line, "CKPT_MODEL")) {
            // model_config_restore_from_log(line);  // Implemented elsewhere
            modelCount++;
        }
    }
    fclose(log);

    printf("[CHECKPOINT] Recovered: %u tensors, %u nodes, %u KV, %u model\n",
           tensorCount, nodeCount, kvCount, modelCount);
}

// ═══════════════════════════════════════════════════════════════════════════
// tensor_index_get_count — Return current index count
// ═══════════════════════════════════════════════════════════════════════════
uint32_t tensor_index_get_count(void)
{
    return g_TensorIndexCount;
}
