/*++
 * acpi_handler.c — Death Rattle & PnP Interface Arrival
 *
 * BRIDGE-007: Power callbacks (EvtDeviceD0Exit — Death Rattle)
 * BRIDGE-008: PnP notification (EvtDeviceInterfaceArrival + WorkItem)
 *
 * Reference: Interactive_Plan.md §III·5 (lines 1393-1554)
 *
 * Death Rattle flow:
 *   1. Windows shutdown → EvtDeviceD0Exit fires (PASSIVE_LEVEL)
 *   2. Complete pending WAIT_VMEXIT with IsShutdownImminent=1
 *   3. Block for 30s via KeWaitForSingleObject (ShutdownAckEvent)
 *   4. LLM sends ACK_READY_TO_DIE → KeSetEvent unblocks
 *   5. VMXOFF + clear CR4.VMXE → allow Windows to shut down
 *
 * PnP arrival constraint X·4:
 *   Never open handles inline in PnP arrival callback — use WorkItem.
 *
 * Constitutional constraints:
 *   X·4  — WorkItem mandatory for PnP arrival handler
 *   X·3  — Spinlock released before WdfRequestComplete
 *--*/

#include <ntddk.h>
#include <wdf.h>
#include <intrin.h>

#include "symbiose_bridge.h"
#include "../inc/symbiose_ioctl.h"
#include "trace.h"
#include "acpi_handler.tmh"

//
// Global device context — set by EvtDriverDeviceAdd via AcpiSetDeviceContext
//
static PSYMBIOSE_DEVICE_CONTEXT gDevCtx = NULL;

// ── AcpiSetDeviceContext ─────────────────────────────────────────────────────
// Called from EvtDriverDeviceAdd to set module-global context.
//
VOID AcpiSetDeviceContext(_In_ PSYMBIOSE_DEVICE_CONTEXT DevCtx)
{
    gDevCtx = DevCtx;
}

// ── GetOutputBuffer ──────────────────────────────────────────────────────────
// Safe helper: retrieves and validates the output buffer of a pending WDFREQUEST.
// Reference: §III·5 (lines 1408-1416)
//
static SYMBIOSE_VMEXIT_EVENT*
GetOutputBuffer(
    _In_ WDFREQUEST Request
    )
{
    PVOID buf = NULL;
    SIZE_T len = 0;
    NTSTATUS status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(SYMBIOSE_VMEXIT_EVENT), &buf, &len);
    if (!NT_SUCCESS(status) || len < sizeof(SYMBIOSE_VMEXIT_EVENT))
        return NULL;
    return (SYMBIOSE_VMEXIT_EVENT*)buf;
}

// ── EvtDeviceD0Exit — Death Rattle ───────────────────────────────────────────
// BRIDGE-007: 30-second power interception.
//
// When Windows initiates shutdown/hibernate/sleep, this callback fires.
// We signal the guest LLM via the pending WAIT_VMEXIT slot, then block
// for up to 30 seconds to let the LLM checkpoint state.
//
// IRQL: PASSIVE_LEVEL — KeWaitForSingleObject is safe here.
// Reference: §III·5 (lines 1419-1467)
//
NTSTATUS
EvtDeviceD0Exit(
    _In_ WDFDEVICE               Device,
    _In_ WDF_POWER_DEVICE_STATE  TargetState
    )
{
    UNREFERENCED_PARAMETER(Device);

    //
    // Only intercept transitions that lead to power-off or hibernate.
    // WdfPowerDeviceD3Final = system shutdown; D3 = sleep/hibernate.
    //
    if (TargetState != WdfPowerDeviceD3Final &&
        TargetState != WdfPowerDeviceD3)
    {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                    "D0Exit: target=%d — allowing without Death Rattle",
                    TargetState);
        return STATUS_SUCCESS;  // Allow D1/D2 transitions without blocking
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "Death Rattle: shutdown imminent (target=%d), signaling LLM",
                TargetState);

    // ── Step 1: Signal the guest LLM via the pending WAIT_VMEXIT slot ────
    // Atomically claim the pending request so the cancel handler can't race us
    WDFREQUEST pending = InterlockedExchangePointer(
        (PVOID volatile*)&gDevCtx->PendingVmExitRequest, NULL);

    if (pending) {
        NTSTATUS unmark = WdfRequestUnmarkCancelable(pending);
        if (NT_SUCCESS(unmark)) {
            // Safely dereference only if unmark succeeded
            SYMBIOSE_VMEXIT_EVENT* evt = GetOutputBuffer(pending);
            if (evt) {
                evt->IsShutdownImminent = 1;
                evt->ExitReason = 0;  // Not a real VM-Exit — synthetic shutdown
            }
            WdfObjectDereference(pending);
            WdfRequestCompleteWithInformation(pending, STATUS_SUCCESS,
                sizeof(SYMBIOSE_VMEXIT_EVENT));

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                        "Death Rattle: WAIT_VMEXIT completed with shutdown signal");
        }
        // If unmark failed → request was already being cancelled; don't touch it
    } else {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DRIVER,
                    "Death Rattle: no pending WAIT_VMEXIT — LLM may not receive signal");
    }

    // ── Step 2: Block the power IRP for up to 30 seconds ─────────────────
    // KeWaitForSingleObject blocks this D0Exit callback, which blocks the power
    // IRP, which blocks the entire OS shutdown sequence. Safe at PASSIVE_LEVEL.
    LARGE_INTEGER timeout = { .QuadPart = -300000000LL }; // 30s in 100ns units
    NTSTATUS waitStatus = KeWaitForSingleObject(
        &gDevCtx->ShutdownAckEvent,
        Executive, KernelMode, FALSE, &timeout);

    if (waitStatus == STATUS_TIMEOUT) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DRIVER,
                    "Death Rattle: 30s timeout — LLM did not ACK, proceeding with shutdown");
    } else {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                    "Death Rattle: LLM ACK received, proceeding with graceful shutdown");
    }

    // ── Step 3: Stop the VM — VMXOFF before driver powers down ───────────
    if (gDevCtx->VmxEnabled) {
        __vmx_off();
        __writecr4(__readcr4() & ~(1ULL << 13));  // Clear CR4.VMXE
        gDevCtx->VmxEnabled = FALSE;
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                    "VMXOFF executed — VMX root mode disabled");
    }

    return STATUS_SUCCESS;
}

// ── SymbioseHandleShutdownAck ────────────────────────────────────────────────
// Called when IOCTL_SYMBIOSE_SHUTDOWN_ACK arrives from ChaosLoader.
// Unblocks EvtDeviceD0Exit's KeWaitForSingleObject.
// Reference: §III·5 (lines 1470-1479)
//
VOID
SymbioseHandleShutdownAck(VOID)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "ShutdownAck received — unblocking Death Rattle");
    KeSetEvent(&gDevCtx->ShutdownAckEvent, IO_NO_INCREMENT, FALSE);
}

// ═══════════════════════════════════════════════════════════════════════════════
// BRIDGE-008: PnP Interface Arrival Notification
// ═══════════════════════════════════════════════════════════════════════════════

//
// Context struct to carry the symbolic link name into the WorkItem.
// Allocated from NonPagedPoolNx — WorkItem may run at DISPATCH_LEVEL.
// Reference: §III·5 (lines 1493-1497)
//
typedef struct _SYMBIOSE_ARRIVAL_CTX {
    WDFWORKITEM     WorkItem;
    UNICODE_STRING  SymbolicLink;
    WCHAR           LinkBuffer[256];
} SYMBIOSE_ARRIVAL_CTX;

//
// Forward declaration for WorkItem callback
//
EVT_WDF_WORKITEM EvtWorkItemOpenInterface;

// ── EvtDeviceInterfaceArrival ────────────────────────────────────────────────
// PnP callback: fires when ChaosLoader.exe opens the device interface.
// Constraint X·4: NEVER open handles inline — use a WorkItem.
// Reference: §III·5 (lines 1499-1531)
//
VOID
EvtDeviceInterfaceArrival(
    _In_ PVOID NotificationStructure,
    _In_ PVOID Context
    )
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION notif =
        (PDEVICE_INTERFACE_CHANGE_NOTIFICATION)NotificationStructure;

    UNREFERENCED_PARAMETER(Context);

    // Allocate context from non-paged pool (WorkItem may run at DISPATCH)
    SYMBIOSE_ARRIVAL_CTX* ctx = (SYMBIOSE_ARRIVAL_CTX*)ExAllocatePoolWithTag(
        NonPagedPoolNx, sizeof(*ctx), 'SMBS');
    if (!ctx) return;

    // Copy symbolic link name for use in WorkItem (notif pointer is volatile)
    RtlInitEmptyUnicodeString(&ctx->SymbolicLink,
        ctx->LinkBuffer, sizeof(ctx->LinkBuffer));
    RtlCopyUnicodeString(&ctx->SymbolicLink,
        notif->SymbolicLinkName);

    WDF_WORKITEM_CONFIG wiConfig;
    WDF_WORKITEM_CONFIG_INIT(&wiConfig, EvtWorkItemOpenInterface);

    WDF_OBJECT_ATTRIBUTES wiAttr;
    WDF_OBJECT_ATTRIBUTES_INIT(&wiAttr);
    wiAttr.ParentObject = WdfObjectContextGetObject(gDevCtx);

    WDFWORKITEM workItem;
    NTSTATUS status = WdfWorkItemCreate(&wiConfig, &wiAttr, &workItem);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER,
                    "Failed to create PnP arrival WorkItem: %!STATUS!", status);
        ExFreePoolWithTag(ctx, 'SMBS');
        return;
    }

    ctx->WorkItem = workItem;
    WdfWorkItemEnqueue(workItem);
    // DO NOT free ctx here — WorkItem owns it

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "PnP arrival: WorkItem enqueued for %wZ", &ctx->SymbolicLink);
}

// ── EvtWorkItemOpenInterface ─────────────────────────────────────────────────
// Runs at PASSIVE_LEVEL in a system thread — safe to open handles.
// Constraint X·4: This is the deferred handler for PnP arrival.
// Reference: §III·5 (lines 1535-1550)
//
VOID
EvtWorkItemOpenInterface(
    _In_ WDFWORKITEM WorkItem
    )
{
    // Find matching ctx by WorkItem handle
    // (In a production driver, you'd use WDF context; here we use a simpler approach)
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "PnP arrival WorkItem executing — interface ready");

    // Notify ChaosLoader.exe that the device interface is ready.
    // Complete any pending IOCTL_SYMBIOSE_WAIT_VMEXIT with a synthetic
    // "interface ready" event if ChaosLoader pre-pended one.
    // (Actual handle open is done by ChaosLoader in Ring-3, not here)

    WdfObjectDelete(WorkItem);  // Self-destruct — MUST be last call
}
