/*++
 * kernel_ioctls.c — IOCTL Wrappers for symbiose_bridge.sys
 *
 * HIVE-LOADER-002: All DeviceIoControl calls to the KMDF driver
 *
 * Reference: Interactive_Plan.md §IV·1 (lines 2016-2054), §V·1 (lines 2213-2227)
 *
 * Purpose:
 *   Thin wrappers around DeviceIoControl for each IOCTL code.
 *   Uses OVERLAPPED (async) for WAIT_VMEXIT and synchronous
 *   for all boot-sequence IOCTLs.
 *
 * Constraint X·1: NO WHPX — all operations via KMDF IOCTLs only.
 *--*/

#include <windows.h>
#include <stdio.h>

#include "../../02_Symbiose_Bridge/inc/symbiose_ioctl.h"

// ── Helper: synchronous DeviceIoControl wrapper ─────────────────────────────
static BOOL SyncIoctl(HANDLE hDevice, DWORD code,
                       PVOID inBuf, DWORD inSize,
                       PVOID outBuf, DWORD outSize,
                       DWORD* bytesReturned)
{
    DWORD dummy = 0;
    if (!bytesReturned) bytesReturned = &dummy;

    OVERLAPPED ov;
    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return FALSE;

    BOOL result = DeviceIoControl(
        hDevice, code,
        inBuf, inSize,
        outBuf, outSize,
        bytesReturned,
        &ov);

    if (!result) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            // Wait for completion
            WaitForSingleObject(ov.hEvent, INFINITE);
            result = GetOverlappedResult(hDevice, &ov, bytesReturned, FALSE);
        }
    }

    CloseHandle(ov.hEvent);
    return result;
}

// ── IOCTL_SYMBIOSE_REGISTER_RAM ─────────────────────────────────────────────
//
// Registers the host-side VirtualAlloc'd buffer with the KMDF driver.
// The driver maps this memory into the guest's EPT tables.
//
// Reference: §V·1 (line 2221)
//
BOOL SymbioseRegisterRam(HANDLE hDevice, PVOID buffer,
                          UINT64 sizeBytes, UINT32 numaNode)
{
    SYMBIOSE_RAM_DESC desc;
    desc.HostVirtualAddress = (UINT64)(ULONG_PTR)buffer;
    desc.SizeBytes = sizeBytes;
    desc.NumaNode = numaNode;

    printf("  REGISTER_RAM: VA=0x%llX Size=0x%llX NUMA=%u\n",
           desc.HostVirtualAddress, desc.SizeBytes, desc.NumaNode);

    DWORD bytesReturned;
    BOOL ok = SyncIoctl(hDevice, IOCTL_SYMBIOSE_REGISTER_RAM,
                         &desc, sizeof(desc), NULL, 0, &bytesReturned);
    if (!ok) {
        fprintf(stderr, "  REGISTER_RAM failed: %lu\n", GetLastError());
    }
    return ok;
}

// ── IOCTL_SYMBIOSE_LOAD_KERNEL / LOAD_INITRD ────────────────────────────────
//
// Transfers a payload (bzImage or initrd) into guest RAM at the specified GPA.
//
// Reference: §V·1 (lines 2222-2223)
//
BOOL SymbioseLoadPayload(HANDLE hDevice, DWORD ioctlCode,
                          PVOID data, UINT64 dataSize,
                          UINT64 guestLoadAddr)
{
    SYMBIOSE_PAYLOAD_DESC desc;
    desc.HostBufferVA = (UINT64)(ULONG_PTR)data;
    desc.PayloadSizeBytes = dataSize;
    desc.GuestLoadAddressPA = guestLoadAddr;

    const char* name = (ioctlCode == IOCTL_SYMBIOSE_LOAD_KERNEL)
                        ? "LOAD_KERNEL" : "LOAD_INITRD";

    printf("  %s: VA=0x%llX Size=%llu GPA=0x%llX\n",
           name, desc.HostBufferVA, desc.PayloadSizeBytes,
           desc.GuestLoadAddressPA);

    DWORD bytesReturned;
    BOOL ok = SyncIoctl(hDevice, ioctlCode,
                         &desc, sizeof(desc), NULL, 0, &bytesReturned);
    if (!ok) {
        fprintf(stderr, "  %s failed: %lu\n", name, GetLastError());
    }
    return ok;
}

// ── IOCTL_SYMBIOSE_SET_BOOT_PARAMS ──────────────────────────────────────────
//
// Sends the boot parameters descriptor to the driver.
// The driver builds the Linux Boot Protocol 2.13 zero page in guest RAM.
//
// Reference: §V·1 (line 2224), §V·2 (lines 2253-2317)
//
// Note: The actual zero-page construction happens in the KMDF driver
// (vmx_hypervisor.c). ChaosLoader only sends the descriptor with
// initrd location and guest RAM size.
//
// Note: SYMBIOSE_BOOT_PARAMS_DESC is defined in symbiose_ioctl.h (shared header)

BOOL SymbioseSetBootParams(HANDLE hDevice,
                            UINT64 initrdGpa, UINT64 initrdSize,
                            UINT64 guestRamSize)
{
    SYMBIOSE_BOOT_PARAMS_DESC desc;
    desc.InitrdGpa = initrdGpa;
    desc.InitrdSize = initrdSize;
    desc.GuestRamSize = guestRamSize;

    printf("  SET_BOOT_PARAMS: InitrdGPA=0x%llX Size=%llu RAM=%llu GB\n",
           desc.InitrdGpa, desc.InitrdSize,
           desc.GuestRamSize / (1024ULL * 1024 * 1024));

    DWORD bytesReturned;
    BOOL ok = SyncIoctl(hDevice, IOCTL_SYMBIOSE_SET_BOOT_PARAMS,
                         &desc, sizeof(desc), NULL, 0, &bytesReturned);
    if (!ok) {
        fprintf(stderr, "  SET_BOOT_PARAMS failed: %lu\n", GetLastError());
    }
    return ok;
}

// ── IOCTL_SYMBIOSE_EPT_MAP_SHM ──────────────────────────────────────────────
//
// Maps a shared memory window into the guest's EPT tables.
// This creates the 512MB Neural Bus window that both ChaosLoader
// (host) and hive_mind (guest) share for jumbo payload transport.
//
// Reference: §V·1 (line 2225), §IV·3 (SHM window mapping)
//
BOOL SymbioseEptMapShm(HANDLE hDevice, UINT64 shmSizeBytes, UINT64 guestPa)
{
    SYMBIOSE_EPT_MAP_DESC desc;
    desc.KernelVA = 0;           // Driver allocates via WdfMemoryCreate
    desc.SizeBytes = shmSizeBytes;
    desc.GuestPA = guestPa;

    printf("  EPT_MAP_SHM: Size=%llu MB GuestPA=0x%llX\n",
           desc.SizeBytes / (1024 * 1024), desc.GuestPA);

    DWORD bytesReturned;
    BOOL ok = SyncIoctl(hDevice, IOCTL_SYMBIOSE_EPT_MAP_SHM,
                         &desc, sizeof(desc), NULL, 0, &bytesReturned);
    if (!ok) {
        fprintf(stderr, "  EPT_MAP_SHM failed: %lu\n", GetLastError());
    }
    return ok;
}

// ── IOCTL_SYMBIOSE_VMLAUNCH ─────────────────────────────────────────────────
//
// Triggers the VMLAUNCH instruction in the KMDF driver.
// The driver calls SwitchToChaos.asm which executes VMLAUNCH.
//
// Reference: §V·1 (line 2226)
//
BOOL SymbioseVmLaunch(HANDLE hDevice)
{
    printf("  VMLAUNCH: Entering VMX non-root mode...\n");

    DWORD bytesReturned;
    BOOL ok = SyncIoctl(hDevice, IOCTL_SYMBIOSE_VMLAUNCH,
                         NULL, 0, NULL, 0, &bytesReturned);
    if (!ok) {
        fprintf(stderr, "  VMLAUNCH failed: %lu\n", GetLastError());
    }
    return ok;
}

// ── IOCTL_SYMBIOSE_WAIT_VMEXIT ──────────────────────────────────────────────
//
// Asynchronous inverted-call IOCTL. ChaosLoader dispatches this BEFORE
// the event occurs. The kernel holds it pending and completes it when
// a VM-Exit fires.
//
// Reference: §IV·1 (lines 2016-2054)
//
// Constraint X·17: ChaosLoader must keep one pending WAIT_VMEXIT at all times.
//
BOOL SymbioseWaitVmExit(HANDLE hDevice, SYMBIOSE_VMEXIT_EVENT* evt)
{
    ZeroMemory(evt, sizeof(*evt));

    OVERLAPPED ov;
    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return FALSE;

    // Fire IOCTL asynchronously — kernel will hold it pending
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_SYMBIOSE_WAIT_VMEXIT,
        NULL, 0,                       // No input
        evt, sizeof(*evt),             // Output: filled by kernel on VM-Exit
        &bytesReturned,
        &ov);

    if (!result) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            // Wait for kernel to complete (VM-Exit has occurred)
            WaitForSingleObject(ov.hEvent, INFINITE);
            result = GetOverlappedResult(hDevice, &ov, &bytesReturned, FALSE);
        }
    }

    CloseHandle(ov.hEvent);
    return result;
}
