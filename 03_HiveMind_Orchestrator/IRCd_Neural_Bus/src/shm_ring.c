/*++
 * shm_ring.c — Multi-Slot SHM Ring Buffer (4GB)
 *
 * HIVE-IRC-008  (P0 — Critical Path)
 *
 * Reference:
 *   - Interactive_Plan.md §VII·7 (lines 3114-3213)
 *   - Task matrix (line 4996)
 *   - Verification: §XIII·5 (line 5271)
 *
 * Purpose:
 *   Extends the single 512MB SHM window (§VII·2) into 8 concurrent slots
 *   for parallel tensor I/O. Eliminates head-of-line blocking — a slow
 *   164GB shard transfer in slot 0 doesn't block a 4KB checkpoint in slot 1.
 *
 * Layout (§VII·7 lines 3118-3130):
 *   4GB total = 8 × 512MB slots
 *   Control header at base (4KB aligned)
 *   Slot data at offset 4096 + (slot × 512MB)
 *
 * Slot state machine:
 *   0=FREE → 1=WRITING → 2=READY → 3=READING → 0=FREE
 *
 * Per §VII·7 line 3212:
 *   "All existing symbiose_send_jumbo() calls must be updated to use
 *    shm_ring_acquire_write() → write data → shm_ring_commit()"
 *--*/

#include "symbiose_ircd.h"
#include "jumbo_payload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

// ── Constants (§VII·7 lines 3134-3137) ──────────────────────────────────────
#define SHM_RING_SLOTS      8
#define SHM_SLOT_SIZE       (512ULL * 1024 * 1024)                  // 512MB
#define SHM_RING_TOTAL      (SHM_RING_SLOTS * SHM_SLOT_SIZE)       // 4GB
#define SHM_CONTROL_OFFSET  0

// Slot states
#define SLOT_FREE       0
#define SLOT_WRITING    1
#define SLOT_READY      2
#define SLOT_READING    3

// ── SHM Ring Control Header (§VII·7 lines 3139-3163) ────────────────────────
// Cache-line aligned (64 bytes) to prevent false sharing between
// producer (WriteIdx) and consumer (ReadIdx).
#pragma pack(push, 1)
typedef struct _SHM_RING_CONTROL {
    // Cache-line 0: Producer index
    volatile uint32_t  WriteIdx;            // Next slot to write (producer)
    uint32_t           _pad0[15];           // Pad to 64 bytes

    // Cache-line 1: Consumer index
    volatile uint32_t  ReadIdx;             // Next slot to read (consumer)
    uint32_t           _pad1[15];           // Pad to 64 bytes

    // Slot state array
    volatile uint32_t  SlotState[SHM_RING_SLOTS]; // 0=FREE,1=WRITING,2=READY,3=READING
    uint32_t           _pad2[8];

    // Per-slot metadata (§VII·7 lines 3151-3157)
    struct {
        uint64_t PayloadId;
        uint64_t PayloadSize;
        uint64_t Crc64;
        uint32_t PayloadType;               // Same as SYMBIOSE_JUMBO_HEADER types
        uint32_t SourceChannel;             // Which IRC channel owns this slot
    } SlotMeta[SHM_RING_SLOTS];

    // Lifetime counters (§VII·7 lines 3159-3162)
    uint64_t TotalBytesWritten;
    uint64_t TotalBytesRead;
    uint32_t OverflowCount;                 // Ring-full events (visible on #telemetry)
    uint32_t Ready;                         // 1 = ring initialized
} SHM_RING_CONTROL;
#pragma pack(pop)

// ── Slot data offset macro (§VII·7 lines 3166-3167) ─────────────────────────
// Control header = 4KB, then slot data follows
#define SHM_SLOT_DATA_OFFSET(slot) \
    (4096 + (uint64_t)(slot) * SHM_SLOT_SIZE)

// ── Ring buffer instance ────────────────────────────────────────────────────
static SHM_RING_CONTROL* g_RingControl = NULL;
static void*             g_RingBase = NULL;

#ifdef _WIN32
static HANDLE            g_hMapping = NULL;
#endif

// ── Atomic helpers (Windows MSVC) ───────────────────────────────────────────
#ifdef _WIN32
#define atomic_store_release(ptr, val) \
    do { _InterlockedExchange((volatile long*)(ptr), (long)(val)); } while(0)
#define atomic_fetch_add_release(ptr, val) \
    _InterlockedExchangeAdd((volatile long*)(ptr), (long)(val))
#else
#define atomic_store_release(ptr, val) \
    __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
#define atomic_fetch_add_release(ptr, val) \
    __atomic_fetch_add((ptr), (val), __ATOMIC_RELEASE)
#endif

// ═══════════════════════════════════════════════════════════════════════════
// shm_ring_init — Create and map the 4GB ring buffer
// ═══════════════════════════════════════════════════════════════════════════
int shm_ring_init(void)
{
    uint64_t totalSize = 4096 + SHM_RING_TOTAL; // Control header + 8 slots

#ifdef _WIN32
    // Create named file mapping for the ring buffer
    LARGE_INTEGER mapSize;
    mapSize.QuadPart = totalSize;

    g_hMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        mapSize.HighPart, mapSize.LowPart,
        L"SymbioseOS_SHM_Ring_v1");

    if (!g_hMapping) {
        fprintf(stderr, "[SHM-RING] CreateFileMapping failed: %lu\n",
                GetLastError());
        return -1;
    }

    g_RingBase = MapViewOfFile(
        g_hMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);

    if (!g_RingBase) {
        fprintf(stderr, "[SHM-RING] MapViewOfFile failed: %lu\n",
                GetLastError());
        CloseHandle(g_hMapping);
        g_hMapping = NULL;
        return -1;
    }
#else
    g_RingBase = mmap(NULL, totalSize,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (g_RingBase == MAP_FAILED) {
        perror("[SHM-RING] mmap failed");
        g_RingBase = NULL;
        return -1;
    }
#endif

    // Initialize control header
    g_RingControl = (SHM_RING_CONTROL*)g_RingBase;
    memset(g_RingControl, 0, sizeof(SHM_RING_CONTROL));
    g_RingControl->Ready = 1;

    printf("[SHM-RING] Initialized: %d slots x %lluMB = %lluGB total\n",
           SHM_RING_SLOTS,
           (unsigned long long)(SHM_SLOT_SIZE / (1024*1024)),
           (unsigned long long)(SHM_RING_TOTAL / (1024ULL*1024*1024)));
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// shm_ring_acquire_write — Acquire a slot for writing
//
// Reference: §VII·7 lines 3170-3179
// Returns slot index (0-7) or -1 if ring is full (backpressure).
// ═══════════════════════════════════════════════════════════════════════════
int shm_ring_acquire_write(void)
{
    if (!g_RingControl || !g_RingControl->Ready) return -1;

    uint32_t idx = g_RingControl->WriteIdx % SHM_RING_SLOTS;

    if (g_RingControl->SlotState[idx] != SLOT_FREE) {
        g_RingControl->OverflowCount++;
        return -1;  // Ring full — backpressure (§VII·7 line 3209)
    }

    atomic_store_release(&g_RingControl->SlotState[idx], SLOT_WRITING);
    return (int)idx;
}

// ═══════════════════════════════════════════════════════════════════════════
// shm_ring_commit — Commit a written slot (makes it visible to consumer)
//
// Reference: §VII·7 lines 3182-3186
// ═══════════════════════════════════════════════════════════════════════════
void shm_ring_commit(int slot)
{
    if (!g_RingControl || slot < 0 || slot >= SHM_RING_SLOTS) return;

    atomic_store_release(&g_RingControl->SlotState[slot], SLOT_READY);
    atomic_fetch_add_release(&g_RingControl->WriteIdx, 1);

    g_RingControl->TotalBytesWritten +=
        g_RingControl->SlotMeta[slot].PayloadSize;
}

// ═══════════════════════════════════════════════════════════════════════════
// shm_ring_acquire_read — Acquire a slot for reading
//
// Reference: §VII·7 lines 3189-3195
// Returns slot index or -1 if no ready slots.
// ═══════════════════════════════════════════════════════════════════════════
int shm_ring_acquire_read(void)
{
    if (!g_RingControl || !g_RingControl->Ready) return -1;

    uint32_t idx = g_RingControl->ReadIdx % SHM_RING_SLOTS;

    if (g_RingControl->SlotState[idx] != SLOT_READY) {
        return -1;  // No ready slots
    }

    atomic_store_release(&g_RingControl->SlotState[idx], SLOT_READING);
    return (int)idx;
}

// ═══════════════════════════════════════════════════════════════════════════
// shm_ring_release — Release a read slot back to free pool
//
// Reference: §VII·7 lines 3198-3202
// ═══════════════════════════════════════════════════════════════════════════
void shm_ring_release(int slot)
{
    if (!g_RingControl || slot < 0 || slot >= SHM_RING_SLOTS) return;

    g_RingControl->TotalBytesRead +=
        g_RingControl->SlotMeta[slot].PayloadSize;

    atomic_store_release(&g_RingControl->SlotState[slot], SLOT_FREE);
    atomic_fetch_add_release(&g_RingControl->ReadIdx, 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// shm_ring_get_slot_ptr — Get pointer to slot data area
//
// Returns pointer to the data region for the given slot index.
// ═══════════════════════════════════════════════════════════════════════════
void* shm_ring_get_slot_ptr(int slot)
{
    if (!g_RingBase || slot < 0 || slot >= SHM_RING_SLOTS) return NULL;
    return (uint8_t*)g_RingBase + SHM_SLOT_DATA_OFFSET(slot);
}

// ═══════════════════════════════════════════════════════════════════════════
// shm_ring_set_meta — Set metadata for a slot before commit
// ═══════════════════════════════════════════════════════════════════════════
void shm_ring_set_meta(int slot, uint64_t payloadId, uint64_t payloadSize,
                        uint64_t crc64, uint32_t payloadType,
                        uint32_t sourceChannel)
{
    if (!g_RingControl || slot < 0 || slot >= SHM_RING_SLOTS) return;

    g_RingControl->SlotMeta[slot].PayloadId = payloadId;
    g_RingControl->SlotMeta[slot].PayloadSize = payloadSize;
    g_RingControl->SlotMeta[slot].Crc64 = crc64;
    g_RingControl->SlotMeta[slot].PayloadType = payloadType;
    g_RingControl->SlotMeta[slot].SourceChannel = sourceChannel;
}

// ═══════════════════════════════════════════════════════════════════════════
// shm_ring_write_jumbo — High-level: write a jumbo payload into the ring
//
// Replaces symbiose_send_jumbo() (§VII·7 line 3212):
//   acquire_write → write data → set_meta → commit
// Also sends TAGMSG with slot= field for consumer routing.
// ═══════════════════════════════════════════════════════════════════════════
int shm_ring_write_jumbo(IRC_CLIENT* client, const char* channel,
                          void* data, uint64_t size, uint32_t payloadType)
{
    int slot = shm_ring_acquire_write();
    if (slot < 0) {
        fprintf(stderr, "[SHM-RING] Ring full — backpressure!\n");
        // Report overflow on #telemetry
        if (client) {
            ircd_send_raw(client,
                "PRIVMSG #telemetry :SHM_RING_OVERFLOW count=%u\r\n",
                g_RingControl->OverflowCount);
        }
        return -1;
    }

    // Write data into slot
    void* slotPtr = shm_ring_get_slot_ptr(slot);
    if (!slotPtr) return -1;

    uint64_t copySize = (size > SHM_SLOT_SIZE) ? SHM_SLOT_SIZE : size;
    memcpy(slotPtr, data, (size_t)copySize);

    // Compute CRC64 and set metadata
    static uint64_t seqCounter = 0;
    uint64_t payloadId = ++seqCounter;
    uint64_t crc = crc64_ecma(data, (size_t)copySize);

    shm_ring_set_meta(slot, payloadId, copySize, crc,
                       payloadType, 0);

    // Commit — makes slot visible to consumer
    shm_ring_commit(slot);

    // Send TAGMSG with slot= field (§VII·7 line 3212)
    if (client && channel) {
        ircd_send_raw(client,
            "@symbiose-seq=%llu;symbiose-payload=%llu;"
            "symbiose-crc=%016llX "
            "TAGMSG %s :payload_type=%u size=%llu slot=%d\r\n",
            (unsigned long long)seqCounter,
            (unsigned long long)payloadId,
            (unsigned long long)crc,
            channel, payloadType,
            (unsigned long long)copySize, slot);
    }

    printf("[SHM-RING] Wrote %llu bytes to slot %d (type=%u)\n",
           (unsigned long long)copySize, slot, payloadType);
    return slot;
}

// ═══════════════════════════════════════════════════════════════════════════
// shm_ring_destroy — Cleanup
// ═══════════════════════════════════════════════════════════════════════════
void shm_ring_destroy(void)
{
#ifdef _WIN32
    if (g_RingBase) {
        UnmapViewOfFile(g_RingBase);
        g_RingBase = NULL;
    }
    if (g_hMapping) {
        CloseHandle(g_hMapping);
        g_hMapping = NULL;
    }
#else
    if (g_RingBase) {
        munmap(g_RingBase, 4096 + SHM_RING_TOTAL);
        g_RingBase = NULL;
    }
#endif
    g_RingControl = NULL;
    printf("[SHM-RING] Destroyed\n");
}
