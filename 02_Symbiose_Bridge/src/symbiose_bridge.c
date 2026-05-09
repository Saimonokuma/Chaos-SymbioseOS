/*++
 * symbiose_bridge.c — Device Add, PPO, MMIO Mapping & MSI-X
 *
 * BRIDGE-002: EvtDriverDeviceAdd (PPO, PnP callbacks, device interface)
 * BRIDGE-003: EvtDevicePrepareHardware (BAR MMIO mapping)
 * BRIDGE-004: MSI-X interrupt creation
 *
 * Reference: Interactive_Plan.md §III·2 (lines 847-934)
 *
 * Constitutional constraints enforced:
 *   X·1  — NO WHPX
 *   X·2  — PPO assertion BEFORE WdfDeviceCreate (Step 1)
 *   X·5  — No DpcForIsr when parent at PASSIVE_LEVEL (use WorkItem)
 *--*/

#include <ntddk.h>
#include <wdf.h>
#include <intrin.h>

#include "symbiose_bridge.h"
#include "trace.h"
#include "symbiose_bridge.tmh"   // WPP auto-generated

//
// Forward declarations for interrupt callbacks
//
EVT_WDF_INTERRUPT_ISR       EvtInterruptIsr;
EVT_WDF_INTERRUPT_WORKITEM  EvtInterruptWorkItem;

//
// EvtDriverDeviceAdd — Device creation and initialization
//
// Mandatory call order (§III·2):
//   Step 1: PPO assertion BEFORE WdfDeviceCreate (Constraint X·2)
//   Step 2: Configure PnP power callbacks
//   Step 3: WdfDeviceCreate with context
//   Step 4: Expose device interface GUID
//   Step 5: Create IOCTL queue
//
// Any deviation from this order causes an immediate bugcheck.
//
NTSTATUS
EvtDriverDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS                        status;
    WDFDEVICE                       device;
    WDF_PNPPOWER_EVENT_CALLBACKS    pnpCallbacks;
    WDF_OBJECT_ATTRIBUTES           deviceAttributes;
    PSYMBIOSE_DEVICE_CONTEXT        devCtx;

    UNREFERENCED_PARAMETER(Driver);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "EvtDriverDeviceAdd — configuring SymbioseOS hypervisor device");

    // ── STEP 1: Assert Power Policy Ownership BEFORE WdfDeviceCreate ─────────
    // Constraint X·2: PPO must be set before device creation or bugcheck.
    // This tells WDF that our driver owns the power policy for this device stack.
    WdfDeviceInitSetPowerPolicyOwnership(DeviceInit, TRUE);

    // ── STEP 2: Configure PnP power callbacks ────────────────────────────────
    // These callbacks manage the device's hardware lifecycle:
    //   PrepareHardware: Map BAR MMIO regions (BRIDGE-003)
    //   ReleaseHardware: Unmap BAR MMIO regions
    //   D0Entry:         Device entering working state
    //   D0Exit:          Device leaving working state (Death Rattle, BRIDGE-007)
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDevicePrepareHardware = EvtDevicePrepareHardware;
    pnpCallbacks.EvtDeviceReleaseHardware = EvtDeviceReleaseHardware;
    pnpCallbacks.EvtDeviceD0Entry         = EvtDeviceD0Entry;
    pnpCallbacks.EvtDeviceD0Exit          = EvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

    // ── STEP 3: Create the WDF device object with context ────────────────────
    // The context area holds all per-device state: BARs, interrupts,
    // pending IOCTLs, VMX regions, and guest memory pointers.
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, SYMBIOSE_DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER,
                    "WdfDeviceCreate failed: %!STATUS!", status);
        return status;
    }

    // Initialize the device context to a known state
    devCtx = GetSymbioseDeviceContext(device);
    RtlZeroMemory(devCtx, sizeof(SYMBIOSE_DEVICE_CONTEXT));

    // Create the synchronization spinlock
    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &devCtx->Lock);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER,
                    "WdfSpinLockCreate failed: %!STATUS!", status);
        return status;
    }

    // Capture host CR3 for VMX host state area (needed by BRIDGE-006)
    // This must be done at PASSIVE_LEVEL before any VMX operations.
    devCtx->HostCr3 = __readcr3();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VMX,
                "Host CR3 captured: 0x%llX", devCtx->HostCr3);

    // Initialize the Death Rattle shutdown event (BRIDGE-007, §III·5 line 1402)
    KeInitializeEvent(&devCtx->ShutdownAckEvent, NotificationEvent, FALSE);

    // Set module-global device context for VMX and ACPI handlers
    VmxSetDeviceContext(devCtx);
    AcpiSetDeviceContext(devCtx);

    // ── STEP 4: Expose device interface GUID ─────────────────────────────────
    // ChaosLoader.exe uses this GUID to find and open the device via
    // SetupDiGetClassDevs / CreateFile.
    // Reference: §III·2 Step 4 (line 873-877)
    status = WdfDeviceCreateDeviceInterface(device,
                &GUID_DEVINTERFACE_SYMBIOSE_BRIDGE, NULL);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER,
                    "WdfDeviceCreateDeviceInterface failed: %!STATUS!", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "Device interface GUID exposed — ready for ChaosLoader");

    // ── STEP 5: Create the IOCTL queue (inverted-call) ───────────────────────
    // Parallel dispatch: multiple IOCTLs from different channels can pend at once.
    // Defined in ioctl_handler.c (BRIDGE-005)
    status = SymbioseCreateIoctlQueue(device);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER,
                    "SymbioseCreateIoctlQueue failed: %!STATUS!", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "EvtDriverDeviceAdd complete — device ready");

    return STATUS_SUCCESS;
}

//
// EvtDevicePrepareHardware — Map BAR MMIO and create MSI-X interrupts
//
// Called by WDF when hardware resources are assigned.
// Iterates the translated resource list and:
//   - Maps CmResourceTypeMemory regions (BARs) into kernel VA via MmMapIoSpace
//   - Creates WDF interrupt objects for CmResourceTypeInterrupt entries
//
// Reference: §III·2 (lines 884-933)
//
NTSTATUS
EvtDevicePrepareHardware(
    _In_ WDFDEVICE    Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PSYMBIOSE_DEVICE_CONTEXT devCtx = GetSymbioseDeviceContext(Device);
    ULONG count;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR desc;

    UNREFERENCED_PARAMETER(ResourcesRaw);

    count = WdfCmResourceListGetCount(ResourcesTranslated);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "EvtDevicePrepareHardware — %lu translated resources", count);

    for (ULONG i = 0; i < count; i++) {
        desc = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        if (desc == NULL) {
            continue;
        }

        switch (desc->Type) {

            case CmResourceTypeMemory:
                //
                // Map BAR into kernel virtual address space
                // Reference: §III·2 (lines 894-900)
                //
                if (devCtx->BarCount < SYMBIOSE_MAX_BARS) {
                    devCtx->BarVa[devCtx->BarCount] =
                        MmMapIoSpace(
                            desc->u.Memory.Start,
                            desc->u.Memory.Length,
                            MmNonCached
                            );

                    if (devCtx->BarVa[devCtx->BarCount] != NULL) {
                        devCtx->BarLen[devCtx->BarCount] = desc->u.Memory.Length;

                        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                                    "BAR[%lu] mapped: PA=0x%llX Len=0x%X VA=%p",
                                    devCtx->BarCount,
                                    desc->u.Memory.Start.QuadPart,
                                    desc->u.Memory.Length,
                                    devCtx->BarVa[devCtx->BarCount]);

                        devCtx->BarCount++;
                    } else {
                        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER,
                                    "MmMapIoSpace failed for BAR PA=0x%llX Len=0x%X",
                                    desc->u.Memory.Start.QuadPart,
                                    desc->u.Memory.Length);
                    }
                }
                break;

            case CmResourceTypeInterrupt:
                //
                // Create MSI-X interrupt object (BRIDGE-004)
                // Reference: §III·2 (lines 919-933)
                //
                SymbioseCreateInterrupt(Device, desc);
                break;

            default:
                // Ignore other resource types (ports, DMA, etc.)
                break;
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "PrepareHardware complete: %lu BARs, %lu interrupts",
                devCtx->BarCount, devCtx->InterruptCount);

    return STATUS_SUCCESS;
}

//
// EvtDeviceReleaseHardware — Unmap BAR MMIO regions
//
// Called by WDF when hardware is being released.
// MUST unmap all BARs mapped in EvtDevicePrepareHardware.
//
// Reference: §III·2 (lines 910-916)
//
NTSTATUS
EvtDeviceReleaseHardware(
    _In_ WDFDEVICE    Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PSYMBIOSE_DEVICE_CONTEXT devCtx = GetSymbioseDeviceContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "EvtDeviceReleaseHardware — unmapping %lu BARs", devCtx->BarCount);

    for (ULONG i = 0; i < devCtx->BarCount; i++) {
        if (devCtx->BarVa[i] != NULL) {
            MmUnmapIoSpace(devCtx->BarVa[i], devCtx->BarLen[i]);
            devCtx->BarVa[i] = NULL;

            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                        "BAR[%lu] unmapped (Len=0x%X)", i, devCtx->BarLen[i]);
        }
    }
    devCtx->BarCount = 0;

    return STATUS_SUCCESS;
}

//
// EvtDeviceD0Entry — Device entering working (D0) state
//
// Stub for now — will be extended by BRIDGE-007 (ACPI handler)
//
NTSTATUS
EvtDeviceD0Entry(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    UNREFERENCED_PARAMETER(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ACPI,
                "EvtDeviceD0Entry — previous state: %d", PreviousState);

    return STATUS_SUCCESS;
}

//
// EvtDeviceD0Exit — implemented in acpi_handler.c (BRIDGE-007)
// Contains the Death Rattle protocol: 30s timeout + VMXOFF.
//

//
// SymbioseCreateInterrupt — Create a WDF interrupt object for MSI-X
//
// Uses WorkItem (NOT DpcForIsr) because the parent device executes at
// PASSIVE_LEVEL. Constraint X·5: DpcForIsr at PASSIVE parent = bugcheck.
//
// Reference: §III·2 (lines 919-933)
//
VOID
SymbioseCreateInterrupt(
    _In_ WDFDEVICE                       Device,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc
    )
{
    PSYMBIOSE_DEVICE_CONTEXT devCtx = GetSymbioseDeviceContext(Device);
    WDF_INTERRUPT_CONFIG     intConfig;
    NTSTATUS                 status;

    UNREFERENCED_PARAMETER(Desc);

    if (devCtx->InterruptCount >= SYMBIOSE_MAX_INTERRUPTS) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DRIVER,
                    "Max interrupt count (%d) reached — skipping",
                    SYMBIOSE_MAX_INTERRUPTS);
        return;
    }

    WDF_INTERRUPT_CONFIG_INIT(&intConfig, EvtInterruptIsr, NULL);

    //
    // Use WorkItem instead of DPC for deferred processing.
    // Constraint X·5: parent at PASSIVE_LEVEL → DpcForIsr is FORBIDDEN.
    // Reference: §III·2 (lines 926-929)
    //
    intConfig.EvtInterruptWorkItem = EvtInterruptWorkItem;

    status = WdfInterruptCreate(
                Device,
                &intConfig,
                WDF_NO_OBJECT_ATTRIBUTES,
                &devCtx->Interrupt[devCtx->InterruptCount]
                );

    if (NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                    "MSI-X interrupt[%lu] created successfully",
                    devCtx->InterruptCount);
        devCtx->InterruptCount++;
    } else {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER,
                    "WdfInterruptCreate failed: %!STATUS!", status);
    }
}

//
// EvtInterruptIsr — Interrupt Service Routine (runs at DIRQL)
//
// Minimal work: acknowledge the interrupt and queue the WorkItem.
// All real processing happens in EvtInterruptWorkItem at PASSIVE_LEVEL.
//
BOOLEAN
EvtInterruptIsr(
    _In_ WDFINTERRUPT Interrupt,
    _In_ ULONG        MessageID
    )
{
    UNREFERENCED_PARAMETER(Interrupt);
    UNREFERENCED_PARAMETER(MessageID);

    //
    // Queue the work item for deferred processing at PASSIVE_LEVEL.
    // Returns TRUE to indicate the interrupt was handled.
    //
    WdfInterruptQueueWorkItemForIsr(Interrupt);
    return TRUE;
}

//
// EvtInterruptWorkItem — Deferred interrupt processing at PASSIVE_LEVEL
//
// Stub for now — BRIDGE-006/012 will add VM-Exit processing here.
//
VOID
EvtInterruptWorkItem(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFOBJECT    AssociatedObject
    )
{
    UNREFERENCED_PARAMETER(Interrupt);
    UNREFERENCED_PARAMETER(AssociatedObject);

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DRIVER,
                "EvtInterruptWorkItem — deferred processing");

    // TODO (BRIDGE-012): Process VM-Exit events and complete pending IOCTLs
}
