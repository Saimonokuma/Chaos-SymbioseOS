/*++
 * jumbo_payload.h — Jumbo Payload SHM Transport Header
 *
 * HIVE-IRC-003
 *
 * Reference: Interactive_Plan.md §VII·2 (lines 2578-2643)
 *
 * Purpose:
 *   Defines the SYMBIOSE_JUMBO_HEADER (IRC-layer envelope) and
 *   payload type constants. The actual 512MB SHM window is
 *   created via CreateFileMapping and shared between ChaosLoader
 *   and hive_mind via EPT re-mapping.
 *
 *   IRC messages are limited to 512 bytes — jumbo payloads bypass
 *   this by writing data into SHM and sending only a pointer +
 *   CRC64 checksum over IRC TAGMSG.
 *--*/

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#endif

// ── Jumbo payload magic ─────────────────────────────────────────────────────
#define JUMBO_MAGIC             0x4A4D424F      // "JMBO"
#define JUMBO_VERSION           1
#define JUMBO_SHM_SIZE          (512ULL * 1024 * 1024)  // 512MB
#define JUMBO_SHM_NAME          "SymbioseNeuralBusSHM"

// ── Payload type constants (§VII·2 lines 2595-2597 — authoritative enum) ────
#define PAYLOAD_TEXT            0
#define PAYLOAD_IMAGE           1
#define PAYLOAD_VIDEO_FRAME     2
#define PAYLOAD_AUDIO_IN        3   // PCM
#define PAYLOAD_AUDIO_OUT       4   // TTS
#define PAYLOAD_SCREEN_CAP      5
#define PAYLOAD_MOVIOLA_DELTA   6
#define PAYLOAD_SHARD_DATA      7
#define PAYLOAD_CHECKPOINT      8
#define PAYLOAD_RECON_RESULT    9
#define PAYLOAD_VMEXIT_EVENT    10

// ── Shorthand aliases (§VII·2 lines 2603-2606 — used in IRC message flows) ──
// These map to the same values but match the simplified naming in the spec's
// IRC message examples (symbiose_send_jumbo, handle_recon_message, etc.)
#define PAYLOAD_VMEXIT   PAYLOAD_TEXT           // VM-Exit event (type 0)
#define PAYLOAD_SHARD    PAYLOAD_SHARD_DATA     // Scout parameter shard
#define PAYLOAD_RECON    PAYLOAD_RECON_RESULT   // Scout search result

// ── SYMBIOSE_JUMBO_HEADER — IRC-layer envelope ──────────────────────────────
// Reference: §VII·2 (lines 2588-2600)
//
// Written at the start of the SHM window. Payload bytes follow immediately
// after this 48-byte header.
//
#pragma pack(push, 1)
typedef struct _SYMBIOSE_JUMBO_HEADER {
    uint32_t Magic;             // 0x4A4D424F ("JMBO")
    uint32_t Version;           // 1
    uint64_t PayloadId;         // Unique ID — echoed in TAGMSG for correlation
    uint64_t TotalSize;         // Total payload bytes
    uint64_t Offset;            // Offset within multi-slot sequence
    uint64_t Crc64;             // CRC-64/ECMA-182 of payload bytes
    uint32_t PayloadType;       // See PAYLOAD_* constants above
    uint32_t Reserved;
} SYMBIOSE_JUMBO_HEADER;
#pragma pack(pop)

// ── SHM context ─────────────────────────────────────────────────────────────
typedef struct _JUMBO_SHM_CTX {
#ifdef _WIN32
    HANDLE      hMapping;       // CreateFileMapping handle
#endif
    void*       BaseAddr;       // MapViewOfFile / mmap base
    uint64_t    Size;           // SHM window size
    uint64_t    NextPayloadId;  // Auto-incrementing sequence
} JUMBO_SHM_CTX;

// ── Public API ──────────────────────────────────────────────────────────────

// CRC-64/ECMA-182
uint64_t crc64_ecma(const void* data, size_t len);

// SHM lifecycle
int  jumbo_shm_create(JUMBO_SHM_CTX* ctx);
int  jumbo_shm_open(JUMBO_SHM_CTX* ctx);
void jumbo_shm_close(JUMBO_SHM_CTX* ctx);

// Payload operations
int  jumbo_send(JUMBO_SHM_CTX* ctx, const void* data, size_t size,
                uint32_t payloadType, uint64_t* outPayloadId);
int  jumbo_recv(JUMBO_SHM_CTX* ctx, SYMBIOSE_JUMBO_HEADER* outHdr,
                void** outData);
bool jumbo_validate(JUMBO_SHM_CTX* ctx);

// IRC TAGMSG formatting
int  jumbo_format_tagmsg(const SYMBIOSE_JUMBO_HEADER* hdr,
                          const char* channel, char* outBuf, size_t bufLen);
