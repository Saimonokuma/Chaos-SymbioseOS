/*++
 * symbiose_ioctl.h — Shared IOCTL Interface Contract
 *
 * ⚠️ SHARED: This header is included by BOTH the KMDF driver (Ring-0)
 *   and ChaosLoader.exe (Ring-3). Do NOT use kernel-only types/APIs here.
 *
 * BRIDGE-012: Shared IOCTL control codes
 *
 * Reference: Interactive_Plan.md §XVI·2 (lines 6569-6668)
 *
 * All IOCTLs use METHOD_NEITHER + FILE_ANY_ACCESS.
 * Ring-0 side MUST wrap all buffer access in __try/__except.
 *--*/

#pragma once

//
// Bring in standard types for both kernel and user-mode
//
#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <windows.h>
typedef unsigned __int64 UINT64;
typedef unsigned __int32 UINT32;
typedef unsigned __int16 UINT16;
typedef unsigned __int8  UINT8;
#endif

//
// ── Device Type ──────────────────────────────────────────────────────────────
//
#define SYMBIOSE_DEVICE_TYPE    0x9001

//
// ── IOCTL Control Codes ──────────────────────────────────────────────────────
// Reference: §XVI·2 (lines 6572-6628)
//

// ChaosLoader → Driver: Register guest RAM buffer into EPT tables
// InputBuffer:  SYMBIOSE_RAM_DESC
// OutputBuffer: none
#define IOCTL_SYMBIOSE_REGISTER_RAM \
    CTL_CODE(SYMBIOSE_DEVICE_TYPE, 0x801, METHOD_NEITHER, FILE_ANY_ACCESS)

// ChaosLoader → Driver: Transfer BZIMAGE payload into guest RAM
// InputBuffer:  SYMBIOSE_PAYLOAD_DESC
// OutputBuffer: none
#define IOCTL_SYMBIOSE_LOAD_KERNEL \
    CTL_CODE(SYMBIOSE_DEVICE_TYPE, 0x802, METHOD_NEITHER, FILE_ANY_ACCESS)

// ChaosLoader → Driver: Transfer initrd.img payload into guest RAM
// InputBuffer:  SYMBIOSE_PAYLOAD_DESC
// OutputBuffer: none
#define IOCTL_SYMBIOSE_LOAD_INITRD \
    CTL_CODE(SYMBIOSE_DEVICE_TYPE, 0x803, METHOD_NEITHER, FILE_ANY_ACCESS)

// ChaosLoader → Driver: Send filled boot_params (zero page)
// InputBuffer:  struct boot_params (Linux Boot Protocol 2.13, packed)
// OutputBuffer: none
#define IOCTL_SYMBIOSE_SET_BOOT_PARAMS \
    CTL_CODE(SYMBIOSE_DEVICE_TYPE, 0x804, METHOD_NEITHER, FILE_ANY_ACCESS)

// ChaosLoader → Driver: Execute VMLAUNCH
// InputBuffer:  none
// OutputBuffer: none
#define IOCTL_SYMBIOSE_VMLAUNCH \
    CTL_CODE(SYMBIOSE_DEVICE_TYPE, 0x805, METHOD_NEITHER, FILE_ANY_ACCESS)

// ChaosLoader → Driver: Block until next VM-Exit event (inverted call)
// InputBuffer:  none
// OutputBuffer: SYMBIOSE_VMEXIT_EVENT
#define IOCTL_SYMBIOSE_WAIT_VMEXIT \
    CTL_CODE(SYMBIOSE_DEVICE_TYPE, 0x806, METHOD_NEITHER, FILE_ANY_ACCESS)

// ChaosLoader → Driver: Read pending guest ttyS0 serial output
// InputBuffer:  none
// OutputBuffer: raw bytes (up to 4096)
#define IOCTL_SYMBIOSE_SERIAL_READ \
    CTL_CODE(SYMBIOSE_DEVICE_TYPE, 0x807, METHOD_NEITHER, FILE_ANY_ACCESS)

// ChaosLoader → Driver: Deliver ACK_READY_TO_DIE from LLM
// InputBuffer:  none
// OutputBuffer: none
#define IOCTL_SYMBIOSE_SHUTDOWN_ACK \
    CTL_CODE(SYMBIOSE_DEVICE_TYPE, 0x808, METHOD_NEITHER, FILE_ANY_ACCESS)

// VFS Manager → Driver: Register SHM window into guest EPTs
// InputBuffer:  SYMBIOSE_EPT_MAP_DESC
// OutputBuffer: none
#define IOCTL_SYMBIOSE_EPT_MAP_SHM \
    CTL_CODE(SYMBIOSE_DEVICE_TYPE, 0x809, METHOD_NEITHER, FILE_ANY_ACCESS)

//
// ── Shared Data Structures ───────────────────────────────────────────────────
// METHOD_NEITHER: passed by pointer, no system buffer copy.
// Reference: §XVI·2 (lines 6633-6667)
//

#pragma pack(push, 1)

// IOCTL_SYMBIOSE_REGISTER_RAM input
typedef struct _SYMBIOSE_RAM_DESC {
    UINT64 HostVirtualAddress;   // VirtualAlloc'd buffer in ChaosLoader
    UINT64 SizeBytes;            // Must match ram_gb from symbiose_config.json
    UINT32 NumaNode;             // 0 if numa_pinned == false
} SYMBIOSE_RAM_DESC;

// IOCTL_SYMBIOSE_LOAD_KERNEL / IOCTL_SYMBIOSE_LOAD_INITRD input
typedef struct _SYMBIOSE_PAYLOAD_DESC {
    UINT64 HostBufferVA;         // Pointer to BZIMAGE or initrd bytes
    UINT64 PayloadSizeBytes;
    UINT64 GuestLoadAddressPA;   // Target guest physical address
} SYMBIOSE_PAYLOAD_DESC;

// Request tag for matching async IOCTL responses
typedef UINT64 SYMBIOSE_REQUEST_TAG;

// IOCTL_SYMBIOSE_WAIT_VMEXIT output
typedef struct _SYMBIOSE_VMEXIT_EVENT {
    SYMBIOSE_REQUEST_TAG Tag;    // Echoed from the original WAIT_VMEXIT request
    UINT32 ExitReason;           // VMCS VM_EXIT_REASON (0x4402) — low 16 = basic reason
    UINT64 ExitQualification;    // VMCS 0x6400 — extra context per exit type
    UINT64 GuestRIP;             // GUEST_RIP at time of exit
    UINT64 GuestRAX;             // Useful for I/O exit decoding
    UINT64 GuestCR0;             // For diagnostic dumps on triple fault
    UINT64 GuestCR2;             // Page fault — NOT in VMCS, read via __readcr2() (X·14)
    UINT64 GuestCR3;             // Guest page table root
    UINT8  SerialByte;           // Valid when ExitReason == IO_INSTRUCTION (port 0x3F8)
    UINT8  IsShutdownImminent;   // 1 = Death Rattle (ACPI); 0 = real VM-Exit
    UINT8  Reserved[6];          // Pad to 8-byte alignment
} SYMBIOSE_VMEXIT_EVENT;

// IOCTL_SYMBIOSE_SET_BOOT_PARAMS input
typedef struct _SYMBIOSE_BOOT_PARAMS_DESC {
    UINT64 InitrdGpa;            // Guest physical address of initrd
    UINT64 InitrdSize;           // Initrd size in bytes
    UINT64 GuestRamSize;         // Total guest RAM size
} SYMBIOSE_BOOT_PARAMS_DESC;

// IOCTL_SYMBIOSE_EPT_MAP_SHM input
typedef struct _SYMBIOSE_EPT_MAP_DESC {
    UINT64 KernelVA;             // WdfMemoryCreate buffer VA
    UINT64 SizeBytes;
    UINT64 GuestPA;              // Where the guest Linux driver expects it
} SYMBIOSE_EPT_MAP_DESC;

#pragma pack(pop)
