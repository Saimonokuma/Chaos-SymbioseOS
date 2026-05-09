/*++
 * ioctl_handler.c — Inverted-Call WDFQUEUE and IOCTL Dispatch
 *
 * BRIDGE-005: Inverted-call WDFQUEUE with parallel dispatch
 *
 * Reference: Interactive_Plan.md §III·3 (lines 938-1103)
 *
 * Architecture:
 *   - Synchronous IOCTLs (REGISTER_RAM, LOAD_KERNEL, etc.) are processed
 *     immediately and completed inline.
 *   - Asynchronous IOCTLs (WAIT_VMEXIT, SERIAL_READ, SHUTDOWN_ACK) are
 *     "pended" in per-channel slots. They are completed later by the
 *     VM-Exit WorkItem or serial intercept handler.
 *
 * Constitutional constraints enforced:
 *   X·3  — Spinlock ALWAYS released BEFORE WdfRequestComplete
 *   X·17 — ChaosLoader must keep 3 pending IOCTLs at all times
 *   METHOD_NEITHER — all buffers wrapped in __try/__except
 *--*/

#include <ntddk.h>
#include <wdf.h>

#include "symbiose_bridge.h"
#include "../inc/symbiose_ioctl.h"
#include "trace.h"
#include "ioctl_handler.tmh"   // WPP auto-generated

//
// Forward declarations
//
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL  EvtIoDeviceControl;
EVT_WDF_REQUEST_CANCEL              EvtRequestCancel;

//
// Internal helper prototypes
//
static VOID SymbiosePendRequest(
    _In_ WDFREQUEST              Request,
    _Inout_ WDFREQUEST volatile* Slot
    );

static VOID SymbioseHandleSync(
    _In_ WDFREQUEST Request,
    _In_ ULONG      IoControlCode
    );

//
// Stub forward declarations for sync IOCTL handlers
// These will be implemented in their respective BRIDGE tasks
//
NTSTATUS SymbioseRegisterGuestRam(_In_ SYMBIOSE_RAM_DESC* Desc);
NTSTATUS SymbioseLoadPayload(_In_ SYMBIOSE_PAYLOAD_DESC* Desc, _In_ ULONG IoControlCode);
NTSTATUS SymbioseSetBootParams(_In_ PVOID InputBuf, _In_ SIZE_T InputLen);
NTSTATUS SymbioseEptMapShm(_In_ SYMBIOSE_EPT_MAP_DESC* Desc);

// ── SymbioseCreateIoctlQueue ─────────────────────────────────────────────────
//
// Creates a parallel-dispatch WDFQUEUE for IOCTL requests.
// Parallel dispatch allows multiple IOCTLs from different async channels
// to pend simultaneously — each channel has its own slot.
//
// Reference: §III·3 (lines 954-964)
//
NTSTATUS
SymbioseCreateIoctlQueue(
    _In_ WDFDEVICE Device
    )
{
    PSYMBIOSE_DEVICE_CONTEXT    devCtx = GetSymbioseDeviceContext(Device);
    WDF_IO_QUEUE_CONFIG         queueConfig;
    NTSTATUS                    status;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_IOCTL,
                "Creating inverted-call IOCTL queue (parallel dispatch)");

    //
    // PARALLEL: multiple IOCTLs from different channels can pend at once
    // This is critical — sequential dispatch would deadlock the inverted-call
    // pattern because WAIT_VMEXIT must pend while REGISTER_RAM completes.
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = EvtIoDeviceControl;

    status = WdfIoQueueCreate(
                Device,
                &queueConfig,
                WDF_NO_OBJECT_ATTRIBUTES,
                &devCtx->IoctlQueue
                );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_IOCTL,
                    "WdfIoQueueCreate failed: %!STATUS!", status);
    } else {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_IOCTL,
                    "IOCTL queue created successfully");
    }

    return status;
}

// ── EvtIoDeviceControl ───────────────────────────────────────────────────────
//
// Main IOCTL dispatch: routes requests to sync or async handlers.
//
// Synchronous IOCTLs are processed and completed immediately.
// Async IOCTLs are stored in per-channel slots and completed later
// by the VM-Exit WorkItem (EvtInterruptWorkItem).
//
// Reference: §III·3 (lines 968-1000)
//
VOID
EvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ SIZE_T     OutputBufferLength,
    _In_ SIZE_T     InputBufferLength,
    _In_ ULONG      IoControlCode
    )
{
    WDFDEVICE                   device;
    PSYMBIOSE_DEVICE_CONTEXT    devCtx;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    device = WdfIoQueueGetDevice(Queue);
    devCtx = GetSymbioseDeviceContext(device);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_IOCTL,
                "EvtIoDeviceControl: code=0x%08X", IoControlCode);

    switch (IoControlCode) {

        // ── Synchronous: validate, process, complete immediately ─────────────
        case IOCTL_SYMBIOSE_REGISTER_RAM:
        case IOCTL_SYMBIOSE_LOAD_KERNEL:
        case IOCTL_SYMBIOSE_LOAD_INITRD:
        case IOCTL_SYMBIOSE_SET_BOOT_PARAMS:
        case IOCTL_SYMBIOSE_VMLAUNCH:
        case IOCTL_SYMBIOSE_EPT_MAP_SHM:
            SymbioseHandleSync(Request, IoControlCode);
            return;   // ← request completed inside SymbioseHandleSync

        // ── Async: pend in per-channel slot; completed by VM-Exit WorkItem ───
        case IOCTL_SYMBIOSE_WAIT_VMEXIT:
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_IOCTL,
                        "Pending WAIT_VMEXIT request");
            SymbiosePendRequest(Request, &devCtx->PendingVmExitRequest);
            return;   // ← DO NOT complete here

        case IOCTL_SYMBIOSE_SERIAL_READ:
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_IOCTL,
                        "Pending SERIAL_READ request");
            SymbiosePendRequest(Request, &devCtx->PendingSerialRequest);
            return;

        case IOCTL_SYMBIOSE_SHUTDOWN_ACK:
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_IOCTL,
                        "Pending SHUTDOWN_ACK request");
            SymbiosePendRequest(Request, &devCtx->PendingShutdownRequest);
            return;

        default:
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_IOCTL,
                        "Unknown IOCTL code: 0x%08X", IoControlCode);
            WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
    }
}

// ── SymbiosePendRequest ──────────────────────────────────────────────────────
//
// Safe pending helper for async IOCTLs (inverted-call pattern).
//
// Uses InterlockedCompareExchangePointer to atomically store the request
// in the per-channel slot. If the slot is already occupied (ChaosLoader
// logic error), rejects with STATUS_DEVICE_BUSY.
//
// After storing, marks the request as cancelable. If the request was
// already cancelled before we could mark it, clears the slot and completes.
//
// Reference: §III·3 (lines 1004-1025)
//
static VOID
SymbiosePendRequest(
    _In_    WDFREQUEST              Request,
    _Inout_ WDFREQUEST volatile*    Slot
    )
{
    WDFREQUEST existing;
    NTSTATUS   status;

    //
    // Atomically try to store Request in Slot (only if Slot == NULL)
    //
    existing = InterlockedCompareExchangePointer(
                    (PVOID volatile*)Slot, Request, NULL);

    if (existing != NULL) {
        //
        // Slot already occupied — ChaosLoader logic error.
        // Each channel must have at most one pending request.
        //
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_IOCTL,
                    "PendRequest rejected — slot already occupied");
        WdfRequestComplete(Request, STATUS_DEVICE_BUSY);
        return;
    }

    //
    // Take a reference to prevent the request from being freed while pending
    //
    WdfObjectReference(Request);

    //
    // Mark the request as cancelable AFTER storing in slot — order matters.
    // If the request is cancelled between storing and marking, the cancel
    // callback will find it in the slot and clear it.
    //
    status = WdfRequestMarkCancelableEx(Request, EvtRequestCancel);
    if (!NT_SUCCESS(status)) {
        //
        // Request was already cancelled before we could mark it.
        // Clear the slot and complete with STATUS_CANCELLED.
        //
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_IOCTL,
                    "Request cancelled before MarkCancelableEx");
        InterlockedExchangePointer((PVOID volatile*)Slot, NULL);
        WdfObjectDereference(Request);
        WdfRequestComplete(Request, STATUS_CANCELLED);
    }
}

// ── EvtRequestCancel ─────────────────────────────────────────────────────────
//
// Cancel handler for pending async IOCTLs.
//
// Required — missing this = memory leak on ChaosLoader process exit.
// Scans all 3 pending slots to find which one holds the cancelled request,
// clears the slot, and completes with STATUS_CANCELLED.
//
// Reference: §III·3 (lines 1028-1048)
//
VOID
EvtRequestCancel(
    _In_ WDFREQUEST Request
    )
{
    WDFDEVICE                device;
    PSYMBIOSE_DEVICE_CONTEXT ctx;
    WDFREQUEST               evicted;
    WDFREQUEST*              slots[3];

    device = WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request));
    ctx    = GetSymbioseDeviceContext(device);

    //
    // Check all 3 async channel slots
    //
    slots[0] = (WDFREQUEST*)&ctx->PendingVmExitRequest;
    slots[1] = (WDFREQUEST*)&ctx->PendingSerialRequest;
    slots[2] = (WDFREQUEST*)&ctx->PendingShutdownRequest;

    for (int i = 0; i < 3; i++) {
        evicted = InterlockedCompareExchangePointer(
                        (PVOID volatile*)slots[i], NULL, Request);
        if (evicted == Request) {
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_IOCTL,
                        "Cancel: cleared pending slot[%d]", i);
            WdfObjectDereference(Request);
            break;
        }
    }

    WdfRequestComplete(Request, STATUS_CANCELLED);
}

// ── SymbioseHandleSync ───────────────────────────────────────────────────────
//
// Synchronous IOCTL processing.
//
// All buffer access is wrapped in __try/__except because METHOD_NEITHER
// passes user-mode VAs directly — a crashing ChaosLoader could produce
// bad pointers at any time.
//
// Constraint X·3: No spinlock is held here, so WdfRequestComplete is safe.
//
// Reference: §III·3 (lines 1051-1103)
//
static VOID
SymbioseHandleSync(
    _In_ WDFREQUEST Request,
    _In_ ULONG      IoControlCode
    )
{
    NTSTATUS status;
    PVOID    inputBuf  = NULL;
    PVOID    outputBuf = NULL;
    SIZE_T   inputLen  = 0;
    SIZE_T   outputLen = 0;

    //
    // Retrieve user-mode buffers — METHOD_NEITHER
    // These return user VA pointers; all access must be in __try
    //
    WdfRequestRetrieveInputBuffer(Request,  0, &inputBuf,  &inputLen);
    WdfRequestRetrieveOutputBuffer(Request, 0, &outputBuf, &outputLen);

    __try {
        switch (IoControlCode) {

            case IOCTL_SYMBIOSE_REGISTER_RAM: {
                if (inputLen < sizeof(SYMBIOSE_RAM_DESC)) {
                    status = STATUS_BUFFER_TOO_SMALL;
                    break;
                }
                SYMBIOSE_RAM_DESC* desc = (SYMBIOSE_RAM_DESC*)inputBuf;
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_IOCTL,
                            "REGISTER_RAM: VA=0x%llX Size=0x%llX NUMA=%u",
                            desc->HostVirtualAddress, desc->SizeBytes,
                            desc->NumaNode);
                status = SymbioseRegisterGuestRam(desc);
                break;
            }

            case IOCTL_SYMBIOSE_LOAD_KERNEL:
            case IOCTL_SYMBIOSE_LOAD_INITRD: {
                if (inputLen < sizeof(SYMBIOSE_PAYLOAD_DESC)) {
                    status = STATUS_BUFFER_TOO_SMALL;
                    break;
                }
                SYMBIOSE_PAYLOAD_DESC* desc = (SYMBIOSE_PAYLOAD_DESC*)inputBuf;
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_IOCTL,
                            "LOAD_PAYLOAD: code=0x%X VA=0x%llX Size=0x%llX GPA=0x%llX",
                            IoControlCode,
                            desc->HostBufferVA, desc->PayloadSizeBytes,
                            desc->GuestLoadAddressPA);
                status = SymbioseLoadPayload(desc, IoControlCode);
                break;
            }

            case IOCTL_SYMBIOSE_SET_BOOT_PARAMS:
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_IOCTL,
                            "SET_BOOT_PARAMS: len=%llu", (UINT64)inputLen);
                status = SymbioseSetBootParams(inputBuf, inputLen);
                break;

            case IOCTL_SYMBIOSE_VMLAUNCH:
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                            "VMLAUNCH IOCTL received — entering VMX non-root");
                status = SymbioseVmLaunch();
                break;

            case IOCTL_SYMBIOSE_EPT_MAP_SHM: {
                if (inputLen < sizeof(SYMBIOSE_EPT_MAP_DESC)) {
                    status = STATUS_BUFFER_TOO_SMALL;
                    break;
                }
                SYMBIOSE_EPT_MAP_DESC* desc = (SYMBIOSE_EPT_MAP_DESC*)inputBuf;
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_EPT,
                            "EPT_MAP_SHM: KVA=0x%llX Size=0x%llX GPA=0x%llX",
                            desc->KernelVA, desc->SizeBytes, desc->GuestPA);
                status = SymbioseEptMapShm(desc);
                break;
            }

            default:
                status = STATUS_INVALID_DEVICE_REQUEST;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        //
        // Bad user pointer — ChaosLoader is crashing or misbehaving
        //
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_IOCTL,
                    "Access violation in sync IOCTL 0x%X — bad user pointer",
                    IoControlCode);
        status = STATUS_ACCESS_VIOLATION;
    }

    //
    // Constraint X·3: Spinlock released BEFORE WdfRequestComplete
    // (No spinlock held in this function — safe to complete)
    //
    WdfRequestComplete(Request, status);
}
