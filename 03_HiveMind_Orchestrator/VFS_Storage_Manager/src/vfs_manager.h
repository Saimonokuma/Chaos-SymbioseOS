/*++
 * vfs_manager.h — VFS Storage Manager Header
 *
 * HIVE-VFS-003: VFS structs + EPT SHM mapping prototypes +
 *               METHOD_NEITHER NVMe IOCTL definitions
 *
 * Reference:
 *   - Interactive_Plan.md §IV·3 (lines 2098-2158) — Zero-Copy SHM
 *   - Interactive_Plan.md §XVI·2 (lines 6624-6667) — IOCTL structs
 *   - Interactive_Plan.md §XVI·4 (lines 6697-6709) — SHM_CONTROL_HEADER
 *   - Interactive_Plan.md §XIII·6 (lines 5307-5317) — Verification Gates
 *
 * Architecture:
 *   The VFS header defines:
 *     1. SHM_CONTROL_HEADER — 64-byte handshake struct (§XVI·4)
 *     2. VFS_CONTEXT — driver-side SHM state
 *     3. VFS_NVME_IO_DESC — zero-copy NVMe IOCTL descriptor
 *     4. NVMe IOCTL codes using METHOD_NEITHER (§IV·1)
 *     5. Public API for SHM creation, EPT registration, NVMe I/O
 *
 * Constraint X·1: NO WHPX. All EPT mapping is native VMX.
 *--*/

#pragma once

#include <ntddk.h>
#include <wdf.h>

// Include shared IOCTL definitions (for SYMBIOSE_EPT_MAP_DESC, etc.)
#include "../../../02_Symbiose_Bridge/inc/symbiose_ioctl.h"

// ── WPP Tracing control ─────────────────────────────────────────────────────
// Defined in BRIDGE trace.h — reuse the same provider GUID
#ifndef TRACE_VFS
#define TRACE_VFS   0  // Placeholder; actual WPP flag set via trace.h
#endif

// ═══════════════════════════════════════════════════════════════════════════
// SHM_CONTROL_HEADER — Handshake struct at offset 0 of the SHM window
//
// Reference: §XVI·4 (lines 6697-6709)
//
// Lifecycle:
//   1. KMDF bridge: allocates SHM, writes header, sets Ready=1
//   2. ChaosLoader: maps SHM, polls Ready==1, sets Ready=2
//   3. Guest hive_mind: maps SHM GPA, polls Ready==2, sets GuestAck=1
//   4. Terminal UI: maps same SHM, reads Ready >= 2 before writing
// ═══════════════════════════════════════════════════════════════════════════

#define SHM_CONTROL_MAGIC    0x53484D43  // 'SHMC' LE
#define SHM_CONTROL_VERSION  1

#pragma pack(push, 1)
typedef struct _SHM_CONTROL_HEADER {
    UINT32 Magic;            // 0x53484D43 = 'SHMC' LE
    UINT32 Version;          // 1
    UINT32 Ready;            // 0=not init; 1=bridge ready; 2=guest ready
    UINT32 GuestAck;         // Guest writes 1 after mapping its side
    UINT64 ShmSizeBytes;     // Total window size (536870912 = 512MB)
    UINT64 PayloadOffset;    // Byte offset where first payload begins
    UINT64 PayloadMaxSize;   // ShmSizeBytes - PayloadOffset
    UINT8  Reserved[24];     // Pad to 64-byte header
} SHM_CONTROL_HEADER;
#pragma pack(pop)

// Static assert: SHM control header MUST be exactly 64 bytes (§XVI·4 line 6709)
C_ASSERT(sizeof(SHM_CONTROL_HEADER) == 64);

// ═══════════════════════════════════════════════════════════════════════════
// VFS_CONTEXT — Driver-side SHM state
//
// Tracks the kernel VA, user VA, MDL, physical address, EPT registration
// status, and guest PA for the shared memory window.
// ═══════════════════════════════════════════════════════════════════════════

typedef struct _VFS_CONTEXT {
    // SHM kernel allocation (HIVE-VFS-001)
    PVOID               ShmKernelVa;     // MmAllocateContiguousMemory result
    PVOID               ShmUserVa;       // MmMapLockedPagesSpecifyCache result
    MDL*                ShmMdl;          // MDL for the SHM allocation
    PHYSICAL_ADDRESS    ShmPhysAddr;     // Physical address of SHM start
    UINT64              ShmSizeBytes;    // Size of the SHM window

    // EPT registration state (HIVE-VFS-002)
    UINT64              ShmGuestPa;      // Guest physical address for EPT mapping
    BOOLEAN             EptRegistered;   // TRUE after VfsEptRegisterShm succeeds

    // Lifecycle
    BOOLEAN             Initialized;     // TRUE after VfsShmCreate succeeds
} VFS_CONTEXT;

// ═══════════════════════════════════════════════════════════════════════════
// VFS_NVME_IO_DESC — Zero-copy NVMe IOCTL descriptor (HIVE-VFS-003)
//
// Used with METHOD_NEITHER IOCTLs. The UserBufferVA points directly
// into user-mode address space — NO intermediate kernel buffer copy.
//
// All dereferences MUST be wrapped in __try/__except per §IV·1 (line 1112).
// ═══════════════════════════════════════════════════════════════════════════

#define VFS_SECTOR_SIZE  512    // Standard NVMe sector size

#pragma pack(push, 1)
typedef struct _VFS_NVME_IO_DESC {
    UINT64 LbaStart;         // Starting Logical Block Address
    UINT32 SectorCount;      // Number of sectors to read/write
    UINT64 UserBufferVA;     // User-mode VA — METHOD_NEITHER direct pointer
    UINT32 DriveIndex;       // CCD drive index (from symbiose_config.json)
    UINT32 Flags;            // Reserved for future (e.g., FUA, bypass cache)
} VFS_NVME_IO_DESC;
#pragma pack(pop)

// ═══════════════════════════════════════════════════════════════════════════
// NVMe IOCTL definitions — METHOD_NEITHER (zero-copy)
//
// Reference: §IV·1 (line 1060), §XVI·2
//
// These extend the IOCTL range defined in symbiose_ioctl.h.
// Function codes 0x80A+ to avoid collision with BRIDGE IOCTLs.
// ═══════════════════════════════════════════════════════════════════════════

#define IOCTL_SYMBIOSE_VFS_NVME_READ \
    CTL_CODE(SYMBIOSE_DEVICE_TYPE, 0x80A, METHOD_NEITHER, FILE_ANY_ACCESS)

#define IOCTL_SYMBIOSE_VFS_NVME_WRITE \
    CTL_CODE(SYMBIOSE_DEVICE_TYPE, 0x80B, METHOD_NEITHER, FILE_ANY_ACCESS)

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

// ── HIVE-VFS-001: Kernel SHM Window ────────────────────────────────────────
// Creates physically contiguous memory, MDL, and maps into user-mode VA.
// Initializes SHM_CONTROL_HEADER with Ready=1.
NTSTATUS VfsShmCreate(VFS_CONTEXT* ctx, UINT64 sizeBytes);

// ── HIVE-VFS-002: EPT Registration ─────────────────────────────────────────
// Maps SHM physical pages into guest address space via EPT page table walk.
// Issues INVEPT to flush stale entries.
NTSTATUS VfsEptRegisterShm(VFS_CONTEXT* ctx, UINT64 guestPa,
                            PVOID eptPointer);

// ── HIVE-VFS-003: NVMe Zero-Copy I/O ──────────────────────────────────────
// METHOD_NEITHER handlers — user VA passed directly, no system buffer copy.
// All user-mode dereferences wrapped in __try/__except.
NTSTATUS VfsNvmeRead(VFS_CONTEXT* ctx, VFS_NVME_IO_DESC* ioDesc);
NTSTATUS VfsNvmeWrite(VFS_CONTEXT* ctx, VFS_NVME_IO_DESC* ioDesc);

// ── SHM Lifecycle ──────────────────────────────────────────────────────────
// Poll for guest acknowledgment (GuestAck=1)
NTSTATUS VfsShmPollGuestAck(VFS_CONTEXT* ctx);

// Set Ready state (lifecycle transitions)
NTSTATUS VfsShmSetReady(VFS_CONTEXT* ctx, UINT32 readyState);

// Tear down: unmap, free MDL, free contiguous memory
void VfsShmDestroy(VFS_CONTEXT* ctx);
