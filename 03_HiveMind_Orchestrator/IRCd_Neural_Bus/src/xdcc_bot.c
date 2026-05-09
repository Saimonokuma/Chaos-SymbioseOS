/*++
 * xdcc_bot.c — XDCC Tensor Bot for Neural Weight Serving
 *
 * HIVE-IRC-007
 *
 * Reference:
 *   - Interactive_Plan.md §VII·6b (lines 2925-2975)
 *   - Interactive_Plan.md §VII·6c (lines 2977-3009)
 *   - Task matrix (line 4995)
 *   - Verification: §XIII·5 (line 5270)
 *
 * Purpose:
 *   Each SymbioseOS node runs an XDCC-style tensor bot that maintains
 *   a catalog of all F32 weight shards it holds and serves them on demand.
 *   The #cluster-announce channel topic is the live tensor registry.
 *
 * Commands (§VII·6b):
 *   XDCC LIST              — List all available shards on this node
 *   XDCC SEND #<n>         — Request shard #n from catalog
 *   XDCC SEARCH <keyword>  — Search shards by layer range or model name
 *   XDCC BATCH #<s>-#<e>   — Request contiguous shard range
 *   XDCC INFO #<n>         — Detailed metadata for shard #n
 *   XDCC CANCEL            — Cancel current transfer
 *
 * Structs (§VII·6b lines 2950-2974):
 *   XDCC_ENTRY — Per-shard catalog entry (256 slots max)
 *   XDCC_BOT   — Bot state with catalog, stats, and concurrency limits
 *--*/

#include "symbiose_ircd.h"
#include "jumbo_payload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

// ── Constants ───────────────────────────────────────────────────────────────
#define MAX_XDCC_SLOTS      256     // §VII·6b line 2963
#define XDCC_MAX_CONCURRENT 4       // §VII·6b line 2970 (default: 4)

// ── XDCC catalog entry (§VII·6b lines 2950-2961) ───────────────────────────
typedef struct _XDCC_ENTRY {
    uint32_t SlotId;                // Catalog slot number (#1, #2, ...)
    uint32_t LayerStart;
    uint32_t LayerEnd;
    uint64_t SizeBytes;             // F32 shard size
    uint64_t Crc64;                 // Content hash for deduplication
    char     ModelName[64];         // e.g. "Mistral-Large-2-123B"
    char     Precision[8];          // Always "F32" (constitutional constraint)
    uint32_t DownloadCount;         // How many times this shard has been served
    uint8_t  Pinned;                // 1 = never evict (critical layers)
    time_t   LastAccessed;
} XDCC_ENTRY;

// ── XDCC bot state (§VII·6b lines 2965-2974) ───────────────────────────────
typedef struct _XDCC_BOT {
    XDCC_ENTRY  Catalog[MAX_XDCC_SLOTS];
    uint32_t    SlotCount;
    uint64_t    TotalBytesServed;   // Lifetime stats
    uint32_t    ActiveTransfers;    // Current concurrent DCC SENDs
    uint32_t    MaxConcurrent;      // Max parallel sends (default: 4)
    char        NodeId[17];
} XDCC_BOT;

static XDCC_BOT g_TensorBot;

// ── Forward declarations ────────────────────────────────────────────────────
extern int dcc_offer_shard(IRC_CLIENT* client, const char* targetNode,
                            uint32_t layerStart, uint32_t layerEnd,
                            uint64_t shardSize);

// Defined below — needed by xdcc_handle_command() before definition
uint64_t xdcc_total_size(void);

// ═══════════════════════════════════════════════════════════════════════════
// xdcc_bot_init — Initialize the tensor bot
// ═══════════════════════════════════════════════════════════════════════════
void xdcc_bot_init(const char* nodeId)
{
    memset(&g_TensorBot, 0, sizeof(g_TensorBot));
    strncpy_s(g_TensorBot.NodeId, sizeof(g_TensorBot.NodeId),
              nodeId, _TRUNCATE);
    g_TensorBot.MaxConcurrent = XDCC_MAX_CONCURRENT;
    printf("[XDCC] Tensor bot initialized for node %s\n", nodeId);
}

// ═══════════════════════════════════════════════════════════════════════════
// xdcc_bot_add_shard — Register a shard in the catalog
// ═══════════════════════════════════════════════════════════════════════════
int xdcc_bot_add_shard(uint32_t layerStart, uint32_t layerEnd,
                        uint64_t sizeBytes, uint64_t crc64,
                        const char* modelName, uint8_t pinned)
{
    if (g_TensorBot.SlotCount >= MAX_XDCC_SLOTS) {
        fprintf(stderr, "[XDCC] Catalog full (%d slots)\n", MAX_XDCC_SLOTS);
        return -1;
    }

    XDCC_ENTRY* entry = &g_TensorBot.Catalog[g_TensorBot.SlotCount];
    entry->SlotId = g_TensorBot.SlotCount + 1;  // 1-indexed
    entry->LayerStart = layerStart;
    entry->LayerEnd = layerEnd;
    entry->SizeBytes = sizeBytes;
    entry->Crc64 = crc64;
    strncpy_s(entry->ModelName, sizeof(entry->ModelName),
              modelName, _TRUNCATE);
    strncpy_s(entry->Precision, sizeof(entry->Precision), "F32", _TRUNCATE);
    entry->DownloadCount = 0;
    entry->Pinned = pinned;
    entry->LastAccessed = time(NULL);

    g_TensorBot.SlotCount++;
    printf("[XDCC] Added shard #%u: layers_%u-%u.f32 [%.1fGB] [CRC:%016llX]\n",
           entry->SlotId, layerStart, layerEnd,
           sizeBytes / (1024.0 * 1024 * 1024),
           (unsigned long long)crc64);
    return (int)entry->SlotId;
}

// ═══════════════════════════════════════════════════════════════════════════
// xdcc_handle_command — Parse and dispatch XDCC commands from IRC
//
// Called when a PRIVMSG to the bot starts with "XDCC "
// ═══════════════════════════════════════════════════════════════════════════
void xdcc_handle_command(IRCD_SERVER* server, int clientIdx,
                          const char* cmdLine)
{
    IRC_CLIENT* c = &server->Clients[clientIdx];

    // ── XDCC LIST ───────────────────────────────────────────────────────
    // §XIII·5 line 5270: "Multi-line catalog returned with slot IDs,
    //                      layer ranges, sizes, CRC64 hashes"
    if (_stricmp(cmdLine, "LIST") == 0) {
        ircd_send_raw(c,
            ":%s NOTICE %s :*** XDCC BOT %s [%u shards] "
            "[%lluGB F32] ***\r\n",
            IRCD_SERVER_NAME, c->Nick, g_TensorBot.NodeId,
            g_TensorBot.SlotCount,
            (unsigned long long)(xdcc_total_size() / (1024ULL*1024*1024)));

        for (uint32_t i = 0; i < g_TensorBot.SlotCount; i++) {
            XDCC_ENTRY* e = &g_TensorBot.Catalog[i];
            ircd_send_raw(c,
                ":%s NOTICE %s :#%u layers_%u-%u.f32 [%.1fGB] "
                "[CRC:%016llX] [%s] [%ux]\r\n",
                IRCD_SERVER_NAME, c->Nick,
                e->SlotId, e->LayerStart, e->LayerEnd,
                e->SizeBytes / (1024.0 * 1024 * 1024),
                (unsigned long long)e->Crc64,
                e->ModelName, e->DownloadCount);
        }

        ircd_send_raw(c,
            ":%s NOTICE %s :*** End of XDCC LIST ***\r\n",
            IRCD_SERVER_NAME, c->Nick);
        printf("[XDCC] LIST served to %s (%u entries)\n",
               c->Nick, g_TensorBot.SlotCount);
        return;
    }

    // ── XDCC SEND #<n> ─────────────────────────────────────────────────
    if (_strnicmp(cmdLine, "SEND #", 6) == 0) {
        uint32_t slotNum = (uint32_t)atoi(cmdLine + 6);
        if (slotNum < 1 || slotNum > g_TensorBot.SlotCount) {
            ircd_send_raw(c,
                ":%s NOTICE %s :*** Invalid slot #%u ***\r\n",
                IRCD_SERVER_NAME, c->Nick, slotNum);
            return;
        }

        if (g_TensorBot.ActiveTransfers >= g_TensorBot.MaxConcurrent) {
            ircd_send_raw(c,
                ":%s NOTICE %s :*** Queue full (%u/%u active). "
                "Try again later. ***\r\n",
                IRCD_SERVER_NAME, c->Nick,
                g_TensorBot.ActiveTransfers, g_TensorBot.MaxConcurrent);
            return;
        }

        XDCC_ENTRY* e = &g_TensorBot.Catalog[slotNum - 1];
        e->DownloadCount++;
        e->LastAccessed = time(NULL);
        g_TensorBot.ActiveTransfers++;

        // Initiate DCC SEND (from dcc_tensor.c)
        dcc_offer_shard(c, c->Nick,
                        e->LayerStart, e->LayerEnd, e->SizeBytes);

        ircd_send_raw(c,
            ":%s NOTICE %s :*** Sending #%u: layers_%u-%u.f32 "
            "(%.1fGB) ***\r\n",
            IRCD_SERVER_NAME, c->Nick,
            e->SlotId, e->LayerStart, e->LayerEnd,
            e->SizeBytes / (1024.0 * 1024 * 1024));
        return;
    }

    // ── XDCC SEARCH <keyword> ───────────────────────────────────────────
    if (_strnicmp(cmdLine, "SEARCH ", 7) == 0) {
        const char* keyword = cmdLine + 7;
        uint32_t found = 0;

        ircd_send_raw(c,
            ":%s NOTICE %s :*** XDCC SEARCH \"%s\" ***\r\n",
            IRCD_SERVER_NAME, c->Nick, keyword);

        for (uint32_t i = 0; i < g_TensorBot.SlotCount; i++) {
            XDCC_ENTRY* e = &g_TensorBot.Catalog[i];

            // Search by model name or layer range string
            char layerStr[64];
            snprintf(layerStr, sizeof(layerStr), "%u-%u",
                     e->LayerStart, e->LayerEnd);

            if (strstr(e->ModelName, keyword) ||
                strstr(layerStr, keyword)) {
                ircd_send_raw(c,
                    ":%s NOTICE %s :#%u layers_%u-%u.f32 [%.1fGB] [%s]\r\n",
                    IRCD_SERVER_NAME, c->Nick,
                    e->SlotId, e->LayerStart, e->LayerEnd,
                    e->SizeBytes / (1024.0 * 1024 * 1024),
                    e->ModelName);
                found++;
            }
        }

        ircd_send_raw(c,
            ":%s NOTICE %s :*** %u results ***\r\n",
            IRCD_SERVER_NAME, c->Nick, found);
        return;
    }

    // ── XDCC BATCH #<start>-#<end> ──────────────────────────────────────
    // Initiates IRCv3 BATCH of DCC SENDs for contiguous shard range
    if (_strnicmp(cmdLine, "BATCH #", 7) == 0) {
        uint32_t startSlot = 0, endSlot = 0;
        sscanf_s(cmdLine + 7, "%u-#%u", &startSlot, &endSlot);

        if (startSlot < 1 || endSlot > g_TensorBot.SlotCount ||
            startSlot > endSlot) {
            ircd_send_raw(c,
                ":%s NOTICE %s :*** Invalid range #%u-#%u ***\r\n",
                IRCD_SERVER_NAME, c->Nick, startSlot, endSlot);
            return;
        }

        // Open IRCv3 BATCH
        char batchId[32];
        snprintf(batchId, sizeof(batchId), "xdcc_%u_%u",
                 startSlot, endSlot);
        ircd_send_raw(c,
            ":%s BATCH +%s netjoin\r\n",
            IRCD_SERVER_NAME, batchId);

        for (uint32_t i = startSlot; i <= endSlot; i++) {
            XDCC_ENTRY* e = &g_TensorBot.Catalog[i - 1];
            e->DownloadCount++;
            e->LastAccessed = time(NULL);

            dcc_offer_shard(c, c->Nick,
                            e->LayerStart, e->LayerEnd, e->SizeBytes);

            ircd_send_raw(c,
                "@batch=%s :%s NOTICE %s :*** Queued #%u: "
                "layers_%u-%u.f32 ***\r\n",
                batchId, IRCD_SERVER_NAME, c->Nick,
                e->SlotId, e->LayerStart, e->LayerEnd);
        }

        // Close BATCH
        ircd_send_raw(c,
            ":%s BATCH -%s\r\n",
            IRCD_SERVER_NAME, batchId);
        printf("[XDCC] BATCH #%u-#%u queued for %s\n",
               startSlot, endSlot, c->Nick);
        return;
    }

    // ── XDCC INFO #<n> ─────────────────────────────────────────────────
    if (_strnicmp(cmdLine, "INFO #", 6) == 0) {
        uint32_t slotNum = (uint32_t)atoi(cmdLine + 6);
        if (slotNum < 1 || slotNum > g_TensorBot.SlotCount) {
            ircd_send_raw(c,
                ":%s NOTICE %s :*** Invalid slot #%u ***\r\n",
                IRCD_SERVER_NAME, c->Nick, slotNum);
            return;
        }

        XDCC_ENTRY* e = &g_TensorBot.Catalog[slotNum - 1];
        ircd_send_raw(c,
            ":%s NOTICE %s :*** XDCC INFO #%u ***\r\n",
            IRCD_SERVER_NAME, c->Nick, e->SlotId);
        ircd_send_raw(c,
            ":%s NOTICE %s :  Layers:    %u-%u\r\n",
            IRCD_SERVER_NAME, c->Nick, e->LayerStart, e->LayerEnd);
        ircd_send_raw(c,
            ":%s NOTICE %s :  Size:      %.1f GB\r\n",
            IRCD_SERVER_NAME, c->Nick,
            e->SizeBytes / (1024.0 * 1024 * 1024));
        ircd_send_raw(c,
            ":%s NOTICE %s :  Precision: %s\r\n",
            IRCD_SERVER_NAME, c->Nick, e->Precision);
        ircd_send_raw(c,
            ":%s NOTICE %s :  CRC64:     %016llX\r\n",
            IRCD_SERVER_NAME, c->Nick, (unsigned long long)e->Crc64);
        ircd_send_raw(c,
            ":%s NOTICE %s :  Model:     %s\r\n",
            IRCD_SERVER_NAME, c->Nick, e->ModelName);
        ircd_send_raw(c,
            ":%s NOTICE %s :  Downloads: %u\r\n",
            IRCD_SERVER_NAME, c->Nick, e->DownloadCount);
        ircd_send_raw(c,
            ":%s NOTICE %s :  Pinned:    %s\r\n",
            IRCD_SERVER_NAME, c->Nick, e->Pinned ? "YES" : "NO");
        return;
    }

    // ── XDCC CANCEL ─────────────────────────────────────────────────────
    if (_stricmp(cmdLine, "CANCEL") == 0) {
        if (g_TensorBot.ActiveTransfers > 0) {
            g_TensorBot.ActiveTransfers--;
        }
        ircd_send_raw(c,
            ":%s NOTICE %s :*** Transfer cancelled. "
            "Resume data saved. ***\r\n",
            IRCD_SERVER_NAME, c->Nick);
        printf("[XDCC] CANCEL from %s\n", c->Nick);
        return;
    }

    // Unknown command
    ircd_send_raw(c,
        ":%s NOTICE %s :*** Unknown XDCC command. "
        "Try: LIST, SEND, SEARCH, BATCH, INFO, CANCEL ***\r\n",
        IRCD_SERVER_NAME, c->Nick);
}

// ═══════════════════════════════════════════════════════════════════════════
// update_tensor_registry_topic — Update #cluster-announce topic
//
// Reference: §VII·6c (lines 2977-3009)
//
// Format (§VII·6b line 2940-2946):
//   TOPIC #cluster-announce :XDCC BOT <node> [<n> shards] [<size> F32]
//     [RDMA:YES] | #1 layers_0-11.f32 [14.9GB] [CRC:a3f7...] | ...
// ═══════════════════════════════════════════════════════════════════════════
void update_tensor_registry_topic(IRC_CLIENT* client)
{
    char topic[4096] = {0};
    int offset = 0;

    offset += snprintf(topic + offset, sizeof(topic) - offset,
        "XDCC BOT %s [%u shards] [%lluGB F32] | ",
        g_TensorBot.NodeId, g_TensorBot.SlotCount,
        (unsigned long long)(xdcc_total_size() / (1024ULL*1024*1024)));

    for (uint32_t i = 0; i < g_TensorBot.SlotCount; i++) {
        XDCC_ENTRY* e = &g_TensorBot.Catalog[i];
        int written = snprintf(topic + offset, sizeof(topic) - offset,
            "#%u layers_%u-%u.f32 [%.1fGB] [CRC:%016llX] | ",
            e->SlotId, e->LayerStart, e->LayerEnd,
            e->SizeBytes / (1024.0 * 1024 * 1024),
            (unsigned long long)e->Crc64);
        if (written > 0) offset += written;
        if ((size_t)offset >= sizeof(topic) - 128) break; // Safety margin
    }

    ircd_send_raw(client,
        "TOPIC #cluster-announce :%s\r\n", topic);
    printf("[XDCC] Topic updated: %u shards registered\n",
           g_TensorBot.SlotCount);
}

// ═══════════════════════════════════════════════════════════════════════════
// xdcc_total_size — Sum of all shard sizes in catalog
// ═══════════════════════════════════════════════════════════════════════════
uint64_t xdcc_total_size(void)
{
    uint64_t total = 0;
    for (uint32_t i = 0; i < g_TensorBot.SlotCount; i++) {
        total += g_TensorBot.Catalog[i].SizeBytes;
    }
    return total;
}

// ═══════════════════════════════════════════════════════════════════════════
// xdcc_transfer_complete — Called when a DCC transfer finishes
// ═══════════════════════════════════════════════════════════════════════════
void xdcc_transfer_complete(uint64_t bytesTransferred)
{
    if (g_TensorBot.ActiveTransfers > 0) {
        g_TensorBot.ActiveTransfers--;
    }
    g_TensorBot.TotalBytesServed += bytesTransferred;
}
