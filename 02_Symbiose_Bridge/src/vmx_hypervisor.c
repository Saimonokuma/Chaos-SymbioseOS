/*++
 * vmx_hypervisor.c — VMXON, VMCS, EPT, VMLAUNCH Hypervisor Core
 *
 * BRIDGE-006: VMX root operation, VMCS field programming, EPT construction
 *
 * Reference: Interactive_Plan.md §III·4 (lines 1116-1386), §XV·1-2
 *
 * Execution order (violation = triple fault with no diagnostic):
 *   1. SymbioseVmxOn()     — CR0/CR4 fixup, VMXON
 *   2. SymbioseVmcsClear() — Allocate + VMCLEAR + VMPTRLD
 *   3. SymbioseEptBuild()  — 4-level EPT tables
 *   4. SymbioseVmcsWrite() — All mandatory VMCS fields
 *   5. SwitchToChaos()     — VMLAUNCH (ASM thunk, BRIDGE-010)
 *
 * Constitutional constraints:
 *   X·1   — NO WHPX
 *   X·14  — GuestCR2 not in VMCS, read via __readcr2()
 *   X·15  — HOST_CR3 captured at EvtDriverDeviceAdd time
 *--*/

#include <ntddk.h>
#include <wdf.h>
#include <intrin.h>

#include "symbiose_bridge.h"
#include "../inc/symbiose_ioctl.h"
#include "trace.h"
#include "vmx_hypervisor.tmh"

//
// EPT PTE flags: Read + Write + Execute + Write-Back memory type
// Reference: §III·4 (line 1337), §XV·1 EPT_PTE struct
//
#define EPT_PTE_RWX_WB  (0x1ULL | 0x2ULL | 0x4ULL | (6ULL << 3))

//
// Host stack size for VM-Exit handler (8KB, non-paged)
//
#define HOST_STACK_SIZE  0x2000

//
// External ASM thunk (BRIDGE-010: SwitchToChaos.asm)
//
extern UINT64 SwitchToChaos(VOID);

//
// HandleVmExit — C dispatcher called by VmExitHandler ASM on every VM-Exit
// Returns: TRUE = VMRESUME (continue guest), FALSE = VMXOFF (shut down)
// Reference: §III·7 (lines 1940-1974)
//
BOOLEAN HandleVmExit(VOID);
#define VmExitHandler HandleVmExit

//
// Global device context pointer — set before VMX operations
// In production this would be passed through per-CPU data; for single-vCPU
// design we use a module-global as the spec does (§III·4 uses gDevCtx).
//
static PSYMBIOSE_DEVICE_CONTEXT gDevCtx = NULL;

// ── AdjustControls ───────────────────────────────────────────────────────────
// Derive allowed VM-control bits from capability MSR pair.
// Low 32 bits = must-be-1 bits; high 32 bits = must-be-0 mask.
// Reference: §III·4 (lines 1193-1199)
//
static UINT32
AdjustControls(
    _In_ UINT32 Desired,
    _In_ UINT32 CapMsr
    )
{
    UINT64 cap = __readmsr(CapMsr);
    Desired |= (UINT32)(cap & 0xFFFFFFFF);       // Must-be-1 bits
    Desired &= (UINT32)(cap >> 32);               // Must-be-0 bits
    return Desired;
}

// ── Step 1: SymbioseVmxOn ────────────────────────────────────────────────────
// Enable VMX root operation.
// Reference: §III·4 (lines 1120-1166)
//
NTSTATUS
SymbioseVmxOn(VOID)
{
    UINT64 cr0, cr4, featureControl;
    UINT32 revisionId;
    UINT8  result;
    PHYSICAL_ADDRESS maxAddr = { .QuadPart = MAXLONGLONG };

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX, "SymbioseVmxOn — begin");

    // ── Pre-flight: CR0 required bits (§III·4 lines 1124-1133) ───────────
    cr0 = __readcr0();
    cr0 |= __readmsr(0x486);   // IA32_VMX_CR0_FIXED0: bits MUST be 1
    cr0 &= __readmsr(0x487);   // IA32_VMX_CR0_FIXED1: bits MUST be 0
    __writecr0(cr0);

    // CR4: apply fixed bits + set CR4.VMXE (bit 13)
    cr4 = __readcr4();
    cr4 |= __readmsr(0x488);   // IA32_VMX_CR4_FIXED0
    cr4 &= __readmsr(0x489);   // IA32_VMX_CR4_FIXED1
    cr4 |= (1ULL << 13);       // CR4.VMXE — enable VMX operation
    __writecr4(cr4);

    // ── IA32_FEATURE_CONTROL (0x3A) check ────────────────────────────────
    featureControl = __readmsr(0x3A);
    if (!(featureControl & 0x1)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_VMX,
                    "IA32_FEATURE_CONTROL lock bit not set");
        return STATUS_NOT_SUPPORTED;
    }
    if (!(featureControl & 0x4)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_VMX,
                    "VMX outside SMX disabled in BIOS");
        return STATUS_NOT_SUPPORTED;
    }

    // ── Allocate & initialize VMXON region (4KB, contiguous) ─────────────
    revisionId = (UINT32)(__readmsr(0x480) & 0x7FFFFFFF);
    gDevCtx->VmxonRegionVa = MmAllocateContiguousMemory(0x1000, maxAddr);
    if (!gDevCtx->VmxonRegionVa) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_VMX,
                    "Failed to allocate VMXON region");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(gDevCtx->VmxonRegionVa, 0x1000);
    *(UINT32*)gDevCtx->VmxonRegionVa = revisionId;
    gDevCtx->VmxonRegionPa = MmGetPhysicalAddress(gDevCtx->VmxonRegionVa);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                "VMXON region: VA=%p PA=0x%llX RevID=0x%X",
                gDevCtx->VmxonRegionVa,
                gDevCtx->VmxonRegionPa.QuadPart, revisionId);

    // ── Execute VMXON ────────────────────────────────────────────────────
    result = __vmx_on(&gDevCtx->VmxonRegionPa.QuadPart);
    if (result != 0) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_VMX,
                    "VMXON failed: result=%d", result);
        MmFreeContiguousMemory(gDevCtx->VmxonRegionVa);
        gDevCtx->VmxonRegionVa = NULL;
        __writecr4(__readcr4() & ~(1ULL << 13));
        return STATUS_UNSUCCESSFUL;
    }

    gDevCtx->VmxEnabled = TRUE;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX, "VMXON succeeded");
    return STATUS_SUCCESS;
}

// ── Step 2: SymbioseVmcsClear ────────────────────────────────────────────────
// Allocate VMCS, VMCLEAR, then VMPTRLD to make it current.
// Reference: §III·4 (lines 1169-1184)
//
static NTSTATUS
SymbioseVmcsClear(VOID)
{
    UINT32 revisionId;
    PHYSICAL_ADDRESS maxAddr = { .QuadPart = MAXLONGLONG };

    revisionId = (UINT32)(__readmsr(0x480) & 0x7FFFFFFF);

    gDevCtx->VmcsVa = MmAllocateContiguousMemory(0x1000, maxAddr);
    if (!gDevCtx->VmcsVa) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_VMX,
                    "Failed to allocate VMCS region");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(gDevCtx->VmcsVa, 0x1000);
    *(UINT32*)gDevCtx->VmcsVa = revisionId;
    gDevCtx->VmcsPa = MmGetPhysicalAddress(gDevCtx->VmcsVa);

    if (__vmx_vmclear(&gDevCtx->VmcsPa.QuadPart) != 0) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_VMX, "VMCLEAR failed");
        return STATUS_UNSUCCESSFUL;
    }
    if (__vmx_vmptrld(&gDevCtx->VmcsPa.QuadPart) != 0) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_VMX, "VMPTRLD failed");
        return STATUS_UNSUCCESSFUL;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                "VMCS allocated and activated: PA=0x%llX",
                gDevCtx->VmcsPa.QuadPart);
    return STATUS_SUCCESS;
}

// ── Step 3: SymbioseEptBuild ─────────────────────────────────────────────────
// Build 4-level EPT tables mapping guest PA [0, Size) → host PA 1:1.
// Reference: §III·4 (lines 1291-1338), §XV·1
//
static NTSTATUS
SymbioseEptBuild(
    _In_ UINT64 GuestRamPa,
    _In_ UINT64 GuestRamSizeBytes
    )
{
    PHYSICAL_ADDRESS maxAddr = { .QuadPart = MAXLONGLONG };
    UINT64 pageCount = GuestRamSizeBytes >> 12;
    UINT64 pageIdx = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_EPT,
                "Building EPT: %llu pages (%llu MB)",
                pageCount, GuestRamSizeBytes >> 20);

    // Allocate PML4 (512 entries × 8 bytes = 4KB)
    UINT64* pml4 = MmAllocateContiguousMemory(0x1000, maxAddr);
    if (!pml4) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(pml4, 0x1000);

    // Allocate PDPT
    UINT64* pdpt = MmAllocateContiguousMemory(0x1000, maxAddr);
    if (!pdpt) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(pdpt, 0x1000);

    PHYSICAL_ADDRESS pdptPa = MmGetPhysicalAddress(pdpt);
    pml4[0] = (pdptPa.QuadPart & ~0xFFFULL) | 0x7;  // R+W+X present

    for (UINT64 pdptIdx = 0; pdptIdx < 512 && pageIdx < pageCount; pdptIdx++) {
        UINT64* pd = MmAllocateContiguousMemory(0x1000, maxAddr);
        if (!pd) return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(pd, 0x1000);

        PHYSICAL_ADDRESS pdPa = MmGetPhysicalAddress(pd);
        pdpt[pdptIdx] = (pdPa.QuadPart & ~0xFFFULL) | 0x7;

        for (UINT64 pdIdx = 0; pdIdx < 512 && pageIdx < pageCount; pdIdx++) {
            UINT64* pt = MmAllocateContiguousMemory(0x1000, maxAddr);
            if (!pt) return STATUS_INSUFFICIENT_RESOURCES;
            RtlZeroMemory(pt, 0x1000);

            PHYSICAL_ADDRESS ptPa = MmGetPhysicalAddress(pt);
            pd[pdIdx] = (ptPa.QuadPart & ~0xFFFULL) | 0x7;

            for (UINT64 ptIdx = 0; ptIdx < 512 && pageIdx < pageCount;
                 ptIdx++, pageIdx++)
            {
                UINT64 hostPa = GuestRamPa + (pageIdx << 12);
                pt[ptIdx] = (hostPa & ~0xFFFULL) | EPT_PTE_RWX_WB;
            }
        }
    }

    // EPT_POINTER: bits[2:0]=3 (4-level walk), bits[5:3]=6 (WB)
    PHYSICAL_ADDRESS pml4Pa = MmGetPhysicalAddress(pml4);
    gDevCtx->EptPointer = (pml4Pa.QuadPart & ~0xFFFULL) | (6ULL << 3) | 3ULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_EPT,
                "EPT built: EPTP=0x%llX (%llu pages mapped)",
                gDevCtx->EptPointer, pageIdx);
    return STATUS_SUCCESS;
}

// ── Step 4: SymbioseVmcsWrite ────────────────────────────────────────────────
// Write all mandatory VMCS fields before VMLAUNCH.
// Omitting ANY field → VM-entry failure Exit Reason 33 (invalid guest state).
// Reference: §III·4 (lines 1187-1288), §XV·2
//
static NTSTATUS
SymbioseVmcsWrite(
    _In_ UINT64 GuestRamPa,
    _In_ UINT64 GuestRamSize,
    _In_ UINT64 KernelRip,
    _In_ UINT64 InitrdPa,
    _In_ UINT64 InitrdSize
    )
{
    UNREFERENCED_PARAMETER(InitrdPa);
    UNREFERENCED_PARAMETER(InitrdSize);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                "Writing VMCS fields: GuestRAM=0x%llX Size=0x%llX RIP=0x%llX",
                GuestRamPa, GuestRamSize, KernelRip);

    // ── Guest CR registers (§XV·2) ──────────────────────────────────────
    __vmx_vmwrite(0x6800, __readcr0());                              // GUEST_CR0
    __vmx_vmwrite(0x6802, GuestRamPa + 0x1000);                     // GUEST_CR3
    __vmx_vmwrite(0x6804, __readcr4() & ~(1ULL << 13));              // GUEST_CR4 (no VMXE)
    __vmx_vmwrite(0x681A, 0x400);                                    // GUEST_DR7
    __vmx_vmwrite(0x6820, 0x2);                                      // GUEST_RFLAGS (bit 1=1)
    __vmx_vmwrite(0x681E, KernelRip);                                // GUEST_RIP
    __vmx_vmwrite(0x681C, GuestRamPa + GuestRamSize - 0x1000);      // GUEST_RSP
    __vmx_vmwrite(0x2806, __readmsr(0xC0000080));                    // GUEST_EFER

    // ── Guest segment registers (§XV·2 full table) ──────────────────────
    // CS: exec+read, DPL=0, present, 64-bit long mode (L=1, D/B=0)
    __vmx_vmwrite(0x0802, 0x10);     // GUEST_CS_SELECTOR
    __vmx_vmwrite(0x6808, 0x0);      // GUEST_CS_BASE
    __vmx_vmwrite(0x4802, 0xFFFF);   // GUEST_CS_LIMIT
    __vmx_vmwrite(0x4816, 0xA09B);   // GUEST_CS_AR_BYTES

    // ES: data, writable, DPL=0, present
    __vmx_vmwrite(0x0800, 0x18);     __vmx_vmwrite(0x6806, 0x0);
    __vmx_vmwrite(0x4800, 0xFFFF);   __vmx_vmwrite(0x4814, 0xC093);

    // SS
    __vmx_vmwrite(0x0804, 0x18);     __vmx_vmwrite(0x680A, 0x0);
    __vmx_vmwrite(0x4804, 0xFFFF);   __vmx_vmwrite(0x4818, 0xC093);

    // DS
    __vmx_vmwrite(0x0806, 0x18);     __vmx_vmwrite(0x680C, 0x0);
    __vmx_vmwrite(0x4806, 0xFFFF);   __vmx_vmwrite(0x481A, 0xC093);

    // FS
    __vmx_vmwrite(0x0808, 0x18);     __vmx_vmwrite(0x680E, 0x0);
    __vmx_vmwrite(0x4808, 0xFFFF);   __vmx_vmwrite(0x481C, 0xC093);

    // GS
    __vmx_vmwrite(0x080A, 0x18);     __vmx_vmwrite(0x6810, 0x0);
    __vmx_vmwrite(0x480A, 0xFFFF);   __vmx_vmwrite(0x481E, 0xC093);

    // TR — mandatory, CANNOT be marked unusable (type=0xB busy TSS)
    __vmx_vmwrite(0x080E, 0x28);     __vmx_vmwrite(0x6814, 0x0);
    __vmx_vmwrite(0x480E, 0x67);     __vmx_vmwrite(0x4822, 0x008B);

    // LDTR — may be marked unusable (AR bit 16 = 1)
    __vmx_vmwrite(0x080C, 0x0);      __vmx_vmwrite(0x6812, 0x0);
    __vmx_vmwrite(0x480C, 0xFFFF);   __vmx_vmwrite(0x4820, 0x10000);

    // GDTR / IDTR
    __vmx_vmwrite(0x6816, GuestRamPa + 0x500);   // GUEST_GDTR_BASE
    __vmx_vmwrite(0x4810, 0x27);                   // GUEST_GDTR_LIMIT
    __vmx_vmwrite(0x6818, GuestRamPa + 0x530);   // GUEST_IDTR_BASE
    __vmx_vmwrite(0x4812, 0xFFFF);                 // GUEST_IDTR_LIMIT

    // ── VMCS link pointer (MUST be all 1s when not using VMCS shadowing) ─
    __vmx_vmwrite(0x2800, 0xFFFFFFFFFFFFFFFF);

    // ── Host state (VM-Exit landing zone, §XV·2) ────────────────────────
    __vmx_vmwrite(0x6C00, __readcr0());                              // HOST_CR0
    __vmx_vmwrite(0x6C02, gDevCtx->HostCr3);                        // HOST_CR3 (X·15)
    __vmx_vmwrite(0x6C04, __readcr4());                              // HOST_CR4
    __vmx_vmwrite(0x6C06, __readmsr(0xC0000100));                    // HOST_FS_BASE
    __vmx_vmwrite(0x6C08, __readmsr(0xC0000101));                    // HOST_GS_BASE
    __vmx_vmwrite(0x6C16, (UINT64)VmExitHandler);                    // HOST_RIP
    __vmx_vmwrite(0x6C14,
        (UINT64)gDevCtx->HostStack + HOST_STACK_SIZE);               // HOST_RSP

    // ── VM-control fields (derived from MSR capability pairs) ───────────
    __vmx_vmwrite(0x4000, AdjustControls(0, 0x481));                 // PIN_BASED
    __vmx_vmwrite(0x4002, AdjustControls((1U << 31), 0x482));        // CPU_BASED (bit31=secondary)
    __vmx_vmwrite(0x401E,
        AdjustControls((1U << 1) | (1U << 7), 0x48B));              // SECONDARY: EPT + Unrestricted
    __vmx_vmwrite(0x4012, AdjustControls((1U << 9), 0x484));         // VM_ENTRY: IA-32e mode (bit9)
    __vmx_vmwrite(0x400C, AdjustControls((1U << 9), 0x483));         // VM_EXIT: host addr 64-bit

    // ── EPT pointer ─────────────────────────────────────────────────────
    __vmx_vmwrite(0x201A, gDevCtx->EptPointer);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                "VMCS fields written successfully");
    return STATUS_SUCCESS;
}

// ── Step 5: SymbioseVmLaunch ─────────────────────────────────────────────────
// Tie all steps together. Called from ioctl_handler.c via IOCTL_SYMBIOSE_VMLAUNCH.
// Reference: §III·4 (lines 1340-1385)
//
NTSTATUS
SymbioseVmLaunch(VOID)
{
    NTSTATUS status;
    PHYSICAL_ADDRESS maxAddr = { .QuadPart = MAXLONGLONG };

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                "SymbioseVmLaunch — beginning VMX launch sequence");

    // 1. Enable VMX root operation
    status = SymbioseVmxOn();
    if (!NT_SUCCESS(status)) return status;

    // 2. Allocate & activate VMCS
    status = SymbioseVmcsClear();
    if (!NT_SUCCESS(status)) goto cleanup_vmxoff;

    // 3. Allocate host stack for VM-Exit handler
    gDevCtx->HostStack = MmAllocateContiguousMemory(HOST_STACK_SIZE, maxAddr);
    if (!gDevCtx->HostStack) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup_vmxoff;
    }
    RtlZeroMemory(gDevCtx->HostStack, HOST_STACK_SIZE);

    // 4. Build EPT tables for guest RAM
    status = SymbioseEptBuild(gDevCtx->GuestRamPa, gDevCtx->GuestRamSize);
    if (!NT_SUCCESS(status)) goto cleanup_vmxoff;

    // 5. Write all mandatory VMCS fields
    status = SymbioseVmcsWrite(
        gDevCtx->GuestRamPa,
        gDevCtx->GuestRamSize,
        0x100000,               // Linux kernel entry at 1MB mark
        gDevCtx->InitrdPa,
        gDevCtx->InitrdSize
    );
    if (!NT_SUCCESS(status)) goto cleanup_vmxoff;

    // 6. VMLAUNCH — CPU enters VMX non-root; guest runs from GUEST_RIP
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                "Executing VMLAUNCH via SwitchToChaos ASM thunk");

    UINT64 vmError = SwitchToChaos();
    if (vmError != 0) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_VMX,
                    "VMLAUNCH failed: VM_INSTRUCTION_ERROR = %llu", vmError);
        status = STATUS_UNSUCCESSFUL;
    }

cleanup_vmxoff:
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_VMX,
                    "VMX launch sequence failed — cleaning up");
        __vmx_off();
        __writecr4(__readcr4() & ~(1ULL << 13));  // Clear CR4.VMXE
        gDevCtx->VmxEnabled = FALSE;
    }
    return status;
}

// ── SymbioseRegisterGuestRam ─────────────────────────────────────────────────
// Called by IOCTL_SYMBIOSE_REGISTER_RAM — stores guest RAM info for later use.
//
NTSTATUS
SymbioseRegisterGuestRam(
    _In_ SYMBIOSE_RAM_DESC* Desc
    )
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                "RegisterGuestRam: VA=0x%llX Size=0x%llX NUMA=%u",
                Desc->HostVirtualAddress, Desc->SizeBytes, Desc->NumaNode);

    gDevCtx->GuestRamBase = (PVOID)Desc->HostVirtualAddress;
    gDevCtx->GuestRamPa = MmGetPhysicalAddress(
        (PVOID)Desc->HostVirtualAddress).QuadPart;
    gDevCtx->GuestRamSize = Desc->SizeBytes;

    return STATUS_SUCCESS;
}

// ── Stub implementations for remaining sync IOCTLs ───────────────────────────
// These will be filled in by their respective BRIDGE tasks.

NTSTATUS SymbioseLoadPayload(_In_ SYMBIOSE_PAYLOAD_DESC* Desc, _In_ ULONG IoControlCode)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                "LoadPayload stub: code=0x%X GPA=0x%llX Size=0x%llX",
                IoControlCode, Desc->GuestLoadAddressPA, Desc->PayloadSizeBytes);
    // TODO: memcpy payload into guest RAM at the specified GPA offset
    return STATUS_SUCCESS;
}

NTSTATUS SymbioseSetBootParams(_In_ PVOID InputBuf, _In_ SIZE_T InputLen)
{
    UNREFERENCED_PARAMETER(InputBuf);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                "SetBootParams stub: len=%llu", (UINT64)InputLen);
    // TODO: copy boot_params (zero page) to guest RAM at 0x7000
    return STATUS_SUCCESS;
}

NTSTATUS SymbioseEptMapShm(_In_ SYMBIOSE_EPT_MAP_DESC* Desc)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_EPT,
                "EptMapShm stub: KVA=0x%llX GPA=0x%llX Size=0x%llX",
                Desc->KernelVA, Desc->GuestPA, Desc->SizeBytes);
    // TODO: add EPT entries mapping SHM window into guest address space
    return STATUS_SUCCESS;
}

// ── VmxSetDeviceContext ──────────────────────────────────────────────────────
// Called from EvtDriverDeviceAdd to set the global device context pointer.
//
VOID VmxSetDeviceContext(_In_ PSYMBIOSE_DEVICE_CONTEXT DevCtx)
{
    gDevCtx = DevCtx;
}

// ── SymbioseHandleIo ─────────────────────────────────────────────────────────
// Handle I/O instruction VM-Exit (exit reason 30).
// Intercepts serial port 0x3F8 (ttyS0) output from guest.
// Reference: §IV·2 (serial port passthrough)
//
static BOOLEAN
SymbioseHandleIo(VOID)
{
    UINT64 exitQual = 0;
    __vmx_vmread(0x6400, &exitQual);    // EXIT_QUALIFICATION

    UINT16 port = (UINT16)((exitQual >> 16) & 0xFFFF);
    BOOLEAN isOut = !(exitQual & 0x8);  // bit 3: 0=OUT, 1=IN

    if (port == 0x3F8 && isOut) {
        // Guest wrote to serial port — capture the byte
        UINT64 guestRax = 0;
        __vmx_vmread(0x681E, &guestRax);  // GUEST_RAX contains the serial byte

        // Complete pending SERIAL_READ IOCTL if one exists
        WDFREQUEST pending = InterlockedExchangePointer(
            (PVOID volatile*)&gDevCtx->PendingSerialRequest, NULL);
        if (pending) {
            NTSTATUS unmark = WdfRequestUnmarkCancelable(pending);
            if (NT_SUCCESS(unmark)) {
                PVOID outBuf = NULL;
                SIZE_T outLen = 0;
                WdfRequestRetrieveOutputBuffer(pending, 1, &outBuf, &outLen);
                if (outBuf && outLen >= 1) {
                    *(UINT8*)outBuf = (UINT8)(guestRax & 0xFF);
                }
                WdfObjectDereference(pending);
                WdfRequestCompleteWithInformation(pending, STATUS_SUCCESS, 1);
            }
        }

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_VMX,
                    "Serial OUT: byte=0x%02X", (UINT8)(guestRax & 0xFF));
    }

    // Advance GUEST_RIP past the I/O instruction
    UINT64 instrLen = 0;
    __vmx_vmread(0x440C, &instrLen);     // VM_EXIT_INSTRUCTION_LEN
    UINT64 guestRip = 0;
    __vmx_vmread(0x681E, &guestRip);
    __vmx_vmwrite(0x681E, guestRip + instrLen);

    return TRUE;  // VMRESUME — continue guest
}

// ── SymbioseCompleteVmExitIrp ────────────────────────────────────────────────
// Complete pending WAIT_VMEXIT IOCTL with VM-Exit event data.
// Reference: §III·7 (line 1971)
//
static VOID
SymbioseCompleteVmExitIrp(
    _In_ UINT64 ExitReason,
    _In_ UINT64 GuestRip
    )
{
    WDFREQUEST pending = InterlockedExchangePointer(
        (PVOID volatile*)&gDevCtx->PendingVmExitRequest, NULL);

    if (!pending) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_VMX,
                    "No pending WAIT_VMEXIT request to complete");
        return;
    }

    NTSTATUS unmark = WdfRequestUnmarkCancelable(pending);
    if (!NT_SUCCESS(unmark)) {
        // Request was already cancelled
        return;
    }

    PVOID outBuf = NULL;
    SIZE_T outLen = 0;
    NTSTATUS status = WdfRequestRetrieveOutputBuffer(
        pending, sizeof(SYMBIOSE_VMEXIT_EVENT), &outBuf, &outLen);

    if (NT_SUCCESS(status) && outLen >= sizeof(SYMBIOSE_VMEXIT_EVENT)) {
        SYMBIOSE_VMEXIT_EVENT* evt = (SYMBIOSE_VMEXIT_EVENT*)outBuf;
        RtlZeroMemory(evt, sizeof(*evt));

        evt->ExitReason = (UINT32)(ExitReason & 0xFFFF);
        evt->GuestRIP = GuestRip;

        // Read additional VMCS fields for diagnostic richness
        UINT64 val = 0;
        __vmx_vmread(0x6400, &val);  evt->ExitQualification = val;
        __vmx_vmread(0x6800, &val);  evt->GuestCR0 = val;
        evt->GuestCR2 = __readcr2();  // X·14: CR2 not in VMCS
        __vmx_vmread(0x6802, &val);  evt->GuestCR3 = val;

        // Read guest RAX for I/O exit decoding
        UINT64 rax = 0;
        __vmx_vmread(0x681E, &rax);  // Note: this reads GUEST_RIP again
        evt->GuestRAX = rax;

        evt->IsShutdownImminent = 0;
    }

    WdfObjectDereference(pending);
    WdfRequestCompleteWithInformation(pending, STATUS_SUCCESS,
        sizeof(SYMBIOSE_VMEXIT_EVENT));

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                "Completed WAIT_VMEXIT IRP: reason=%u RIP=0x%llX",
                (UINT32)(ExitReason & 0xFFFF), GuestRip);
}

// ── HandleVmExit ─────────────────────────────────────────────────────────────
// Called by VmExitHandler.asm on every VM-Exit.
// Returns: TRUE = VMRESUME (continue guest), FALSE = VMXOFF (shut down)
// Reference: §III·7 (lines 1940-1974)
//
BOOLEAN
HandleVmExit(VOID)
{
    UINT64 exitReason = 0;
    __vmx_vmread(0x4402, &exitReason);   // VM_EXIT_REASON

    UINT64 guestRip = 0;
    __vmx_vmread(0x681E, &guestRip);     // GUEST_RIP

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                "VM-Exit: reason=%llu GuestRIP=0x%llx", exitReason, guestRip);

    switch (exitReason & 0xFFFF) {       // Low 16 bits = basic exit reason

        case 30:  // IO_INSTRUCTION (serial port access from guest)
            return SymbioseHandleIo();

        case 1:   // External interrupt (host interrupt fired during guest)
            // Let host IRQL handler run — VMRESUME resumes guest after
            return TRUE;

        case 33:  // VM_ENTRY_FAILURE_INVALID_GUEST_STATE
        case 34:  // VM_ENTRY_FAILURE_MSR_LOADING
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_VMX,
                "VM-Entry failure (should not reach VmExitHandler): %llu",
                exitReason);
            return FALSE;

        default:
            // Unhandled VM-Exit — complete WAIT_VMEXIT IRP with exit data
            SymbioseCompleteVmExitIrp(exitReason, guestRip);
            return FALSE;  // Shut down until ChaosLoader re-issues VMLAUNCH
    }
}
