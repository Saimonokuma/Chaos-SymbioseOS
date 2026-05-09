/*++
 * jumbo_payload.c — 512MB SHM Jumbo Payload Transport
 *
 * HIVE-IRC-003
 *
 * Reference: Interactive_Plan.md §VII·2 (lines 2578-2643)
 *
 * Purpose:
 *   Implements the jumbo payload bypass for IRC's 512-byte message limit.
 *   Large payloads (tensor shards, checkpoints, scout results) are written
 *   into a 512MB shared memory window; only a pointer + CRC64 is sent
 *   over IRC TAGMSG.
 *
 * SHM lifecycle:
 *   Host side:  jumbo_shm_create() → CreateFileMapping + MapViewOfFile
 *   Guest side: jumbo_shm_open()   → OpenFileMapping + MapViewOfFile
 *                                     (or mmap at well-known GPA)
 *
 * Constraint X·1: NO WHPX — SHM window is mapped via EPT by the KMDF
 *                 driver, not by any hypervisor API.
 *--*/

#include "jumbo_payload.h"
#include <stdio.h>
#include <string.h>

// ═══════════════════════════════════════════════════════════════════════════
// CRC-64/ECMA-182
// Polynomial: 0x42F0E1EBA9EA3693
// ═══════════════════════════════════════════════════════════════════════════

static uint64_t g_Crc64Table[256];
static int      g_Crc64TableInit = 0;

static void crc64_init_table(void)
{
    const uint64_t poly = 0x42F0E1EBA9EA3693ULL;
    for (int i = 0; i < 256; i++) {
        uint64_t crc = (uint64_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
        g_Crc64Table[i] = crc;
    }
    g_Crc64TableInit = 1;
}

uint64_t crc64_ecma(const void* data, size_t len)
{
    if (!g_Crc64TableInit) crc64_init_table();

    const uint8_t* p = (const uint8_t*)data;
    uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;

    for (size_t i = 0; i < len; i++) {
        uint8_t idx = (uint8_t)(crc ^ p[i]);
        crc = (crc >> 8) ^ g_Crc64Table[idx];
    }

    return crc ^ 0xFFFFFFFFFFFFFFFFULL;
}

// ═══════════════════════════════════════════════════════════════════════════
// SHM lifecycle — Windows (CreateFileMapping)
// ═══════════════════════════════════════════════════════════════════════════

#ifdef _WIN32

int jumbo_shm_create(JUMBO_SHM_CTX* ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->Size = JUMBO_SHM_SIZE;

    // Create named file mapping (backed by pagefile)
    DWORD sizeHigh = (DWORD)(ctx->Size >> 32);
    DWORD sizeLow  = (DWORD)(ctx->Size & 0xFFFFFFFF);

    ctx->hMapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE,       // Pagefile-backed
        NULL,                       // Default security
        PAGE_READWRITE,
        sizeHigh, sizeLow,
        JUMBO_SHM_NAME);

    if (!ctx->hMapping) {
        fprintf(stderr, "[JUMBO] CreateFileMapping failed: %lu\n",
                GetLastError());
        return -1;
    }

    ctx->BaseAddr = MapViewOfFile(
        ctx->hMapping,
        FILE_MAP_ALL_ACCESS,
        0, 0, (SIZE_T)ctx->Size);

    if (!ctx->BaseAddr) {
        fprintf(stderr, "[JUMBO] MapViewOfFile failed: %lu\n",
                GetLastError());
        CloseHandle(ctx->hMapping);
        ctx->hMapping = NULL;
        return -1;
    }

    // Zero the SHM window
    memset(ctx->BaseAddr, 0, (size_t)ctx->Size);
    ctx->NextPayloadId = 1;

    printf("[JUMBO] SHM created: %s (%llu MB)\n",
           JUMBO_SHM_NAME, ctx->Size / (1024 * 1024));
    return 0;
}

int jumbo_shm_open(JUMBO_SHM_CTX* ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->Size = JUMBO_SHM_SIZE;

    ctx->hMapping = OpenFileMappingA(
        FILE_MAP_ALL_ACCESS,
        FALSE,
        JUMBO_SHM_NAME);

    if (!ctx->hMapping) {
        fprintf(stderr, "[JUMBO] OpenFileMapping failed: %lu\n",
                GetLastError());
        return -1;
    }

    ctx->BaseAddr = MapViewOfFile(
        ctx->hMapping,
        FILE_MAP_ALL_ACCESS,
        0, 0, (SIZE_T)ctx->Size);

    if (!ctx->BaseAddr) {
        fprintf(stderr, "[JUMBO] MapViewOfFile failed: %lu\n",
                GetLastError());
        CloseHandle(ctx->hMapping);
        ctx->hMapping = NULL;
        return -1;
    }

    ctx->NextPayloadId = 1;
    printf("[JUMBO] SHM opened: %s (%llu MB)\n",
           JUMBO_SHM_NAME, ctx->Size / (1024 * 1024));
    return 0;
}

void jumbo_shm_close(JUMBO_SHM_CTX* ctx)
{
    if (ctx->BaseAddr) {
        UnmapViewOfFile(ctx->BaseAddr);
        ctx->BaseAddr = NULL;
    }
    if (ctx->hMapping) {
        CloseHandle(ctx->hMapping);
        ctx->hMapping = NULL;
    }
    printf("[JUMBO] SHM closed\n");
}

#else
// ── Linux stub (guest-side uses mmap at well-known GPA) ─────────────────
// Actual implementation lives in hive_mind guest code
int  jumbo_shm_create(JUMBO_SHM_CTX* ctx) { (void)ctx; return -1; }
int  jumbo_shm_open(JUMBO_SHM_CTX* ctx)   { (void)ctx; return -1; }
void jumbo_shm_close(JUMBO_SHM_CTX* ctx)  { (void)ctx; }
#endif

// ═══════════════════════════════════════════════════════════════════════════
// Payload operations
// ═══════════════════════════════════════════════════════════════════════════

// ── Write header + data into SHM, return payload ID ─────────────────────
int jumbo_send(JUMBO_SHM_CTX* ctx, const void* data, size_t size,
               uint32_t payloadType, uint64_t* outPayloadId)
{
    if (!ctx->BaseAddr) return -1;

    // Check size fits in SHM (minus header)
    size_t maxPayload = (size_t)(ctx->Size - sizeof(SYMBIOSE_JUMBO_HEADER));
    if (size > maxPayload) {
        fprintf(stderr, "[JUMBO] Payload too large: %zu > %zu\n",
                size, maxPayload);
        return -1;
    }

    uint64_t payloadId = ctx->NextPayloadId++;

    // Write header at SHM base
    SYMBIOSE_JUMBO_HEADER* hdr = (SYMBIOSE_JUMBO_HEADER*)ctx->BaseAddr;
    hdr->Magic       = JUMBO_MAGIC;
    hdr->Version     = JUMBO_VERSION;
    hdr->PayloadId   = payloadId;
    hdr->TotalSize   = (uint64_t)size;
    hdr->Offset      = 0;
    hdr->Crc64       = crc64_ecma(data, size);
    hdr->PayloadType = payloadType;
    hdr->Reserved    = 0;

    // Copy payload after header
    uint8_t* dest = (uint8_t*)ctx->BaseAddr + sizeof(SYMBIOSE_JUMBO_HEADER);
    memcpy(dest, data, size);

    if (outPayloadId) *outPayloadId = payloadId;

    printf("[JUMBO] Sent payload #%llu: type=%u size=%zu crc=0x%016llX\n",
           (unsigned long long)payloadId, payloadType, size,
           (unsigned long long)hdr->Crc64);
    return 0;
}

// ── Read header + data pointer from SHM ─────────────────────────────────
int jumbo_recv(JUMBO_SHM_CTX* ctx, SYMBIOSE_JUMBO_HEADER* outHdr,
               void** outData)
{
    if (!ctx->BaseAddr) return -1;

    SYMBIOSE_JUMBO_HEADER* hdr = (SYMBIOSE_JUMBO_HEADER*)ctx->BaseAddr;

    // Validate magic
    if (hdr->Magic != JUMBO_MAGIC) {
        fprintf(stderr, "[JUMBO] Bad magic: 0x%08X (expected 0x%08X)\n",
                hdr->Magic, JUMBO_MAGIC);
        return -1;
    }

    if (outHdr) memcpy(outHdr, hdr, sizeof(*outHdr));
    if (outData) {
        *outData = (uint8_t*)ctx->BaseAddr + sizeof(SYMBIOSE_JUMBO_HEADER);
    }
    return 0;
}

// ── Validate CRC64 of payload in SHM ────────────────────────────────────
bool jumbo_validate(JUMBO_SHM_CTX* ctx)
{
    if (!ctx->BaseAddr) return false;

    SYMBIOSE_JUMBO_HEADER* hdr = (SYMBIOSE_JUMBO_HEADER*)ctx->BaseAddr;
    if (hdr->Magic != JUMBO_MAGIC) return false;

    void* payload = (uint8_t*)ctx->BaseAddr + sizeof(SYMBIOSE_JUMBO_HEADER);
    uint64_t computed = crc64_ecma(payload, (size_t)hdr->TotalSize);

    if (computed != hdr->Crc64) {
        fprintf(stderr, "[JUMBO] CRC64 mismatch: computed=0x%016llX "
                "expected=0x%016llX\n",
                (unsigned long long)computed,
                (unsigned long long)hdr->Crc64);
        return false;
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// IRC TAGMSG formatting
// Reference: §VII·2 (lines 2609-2613)
// ═══════════════════════════════════════════════════════════════════════════

int jumbo_format_tagmsg(const SYMBIOSE_JUMBO_HEADER* hdr,
                         const char* channel, char* outBuf, size_t bufLen)
{
    return snprintf(outBuf, bufLen,
        "@symbiose-seq=%llu;symbiose-payload=%llu;"
        "symbiose-crc=%016llX "
        "TAGMSG %s :payload_type=%u size=%llu\r\n",
        (unsigned long long)hdr->PayloadId,
        (unsigned long long)hdr->PayloadId,
        (unsigned long long)hdr->Crc64,
        channel,
        hdr->PayloadType,
        (unsigned long long)hdr->TotalSize);
}
