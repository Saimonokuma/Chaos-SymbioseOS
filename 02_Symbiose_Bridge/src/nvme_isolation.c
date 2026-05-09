/*++
 * nvme_isolation.c — SymbioseNull WDM Upper Filter
 *
 * BRIDGE-009: NVMe storage isolation filter
 *
 * Reference: Interactive_Plan.md §III·6 (lines 1556-1705)
 *
 * This is a WDM driver, NOT KMDF. Do NOT use Wdf* APIs here.
 *
 * Purpose:
 *   Attaches above the NVMe device stack as an upper filter.
 *   Blinds Windows to the target drive — all storage I/O is rejected
 *   with STATUS_NOT_SUPPORTED, giving the Chaos OS guest exclusive
 *   raw block access via EPT-mapped MMIO.
 *
 * IRP handling:
 *   - READ/WRITE/IOCTL/FLUSH/QUERY/SET → STATUS_NOT_SUPPORTED (blocked)
 *   - CREATE/CLOSE/CLEANUP → pass through (needed for PnP enumeration)
 *   - POWER → PoStartNextPowerIrp + PoCallDriver (pass through)
 *   - PNP → pass through; REMOVE_DEVICE detaches and deletes filter DO
 *--*/

#include <ntddk.h>

// ── Filter device extension ──────────────────────────────────────────────────
typedef struct _NULL_FILTER_CONTEXT {
    PDEVICE_OBJECT LowerDeviceObject;  // Next driver in the stack
} NULL_FILTER_CONTEXT;

// ── Forward declarations ─────────────────────────────────────────────────────
DRIVER_ADD_DEVICE SymbioseNullAddDevice;
DRIVER_UNLOAD     SymbioseNullUnload;
DRIVER_DISPATCH   SymbioseNullDispatch;
DRIVER_DISPATCH   SymbioseNullPower;
DRIVER_DISPATCH   SymbioseNullPnp;

// ── DriverEntry ──────────────────────────────────────────────────────────────
// Register dispatch routines and AddDevice callback.
// Reference: §III·6 (lines 1576-1594)
//
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    UNREFERENCED_PARAMETER(RegistryPath);

    // Register AddDevice — called by PnP manager when device is found
    DriverObject->DriverExtension->AddDevice = SymbioseNullAddDevice;
    DriverObject->DriverUnload               = SymbioseNullUnload;

    // Set dispatch routines — ALL major functions default to SymbioseNullDispatch
    for (ULONG i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = SymbioseNullDispatch;
    }

    // Power IRPs need special handling (PoStartNextPowerIrp + PoCallDriver)
    DriverObject->MajorFunction[IRP_MJ_POWER] = SymbioseNullPower;

    // PnP IRPs must pass through — never block them
    DriverObject->MajorFunction[IRP_MJ_PNP]   = SymbioseNullPnp;

    return STATUS_SUCCESS;
}

// ── SymbioseNullUnload ───────────────────────────────────────────────────────
// Reference: §III·6 (lines 1596-1600)
//
VOID
SymbioseNullUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    // Nothing to clean up — device objects are deleted in IRP_MN_REMOVE_DEVICE
}

// ── SymbioseNullAddDevice ────────────────────────────────────────────────────
// Create filter DO and attach above the NVMe device stack.
// Reference: §III·6 (lines 1606-1636)
//
NTSTATUS
SymbioseNullAddDevice(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PDEVICE_OBJECT  PhysicalDeviceObject
    )
{
    PDEVICE_OBJECT filterDo = NULL;
    NTSTATUS status;

    // Create our filter device object
    status = IoCreateDevice(
        DriverObject,
        sizeof(NULL_FILTER_CONTEXT),   // DeviceExtension size
        NULL,                           // No device name needed for filter
        FILE_DEVICE_DISK,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &filterDo);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Attach our filter above the existing NVMe stack
    NULL_FILTER_CONTEXT* ctx = (NULL_FILTER_CONTEXT*)filterDo->DeviceExtension;
    ctx->LowerDeviceObject = IoAttachDeviceToDeviceStack(
        filterDo, PhysicalDeviceObject);

    if (!ctx->LowerDeviceObject) {
        IoDeleteDevice(filterDo);
        return STATUS_NO_SUCH_DEVICE;
    }

    // Mirror the flags from the lower device (buffering, alignment, etc.)
    filterDo->Flags |= ctx->LowerDeviceObject->Flags &
                       (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE);
    filterDo->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

// ── SymbioseNullDispatch ─────────────────────────────────────────────────────
// Handles all IRP_MJ_* EXCEPT Power and PnP.
// Blocks storage I/O; passes through everything else.
// Reference: §III·6 (lines 1643-1667)
//
NTSTATUS
SymbioseNullDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp
    )
{
    PIO_STACK_LOCATION   stack = IoGetCurrentIrpStackLocation(Irp);
    NULL_FILTER_CONTEXT* ctx   = (NULL_FILTER_CONTEXT*)DeviceObject->DeviceExtension;

    switch (stack->MajorFunction) {

        case IRP_MJ_READ:
        case IRP_MJ_WRITE:
        case IRP_MJ_DEVICE_CONTROL:
        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
        case IRP_MJ_FLUSH_BUFFERS:
        case IRP_MJ_QUERY_INFORMATION:
        case IRP_MJ_SET_INFORMATION:
            //
            // Reject all storage I/O — Windows is blind to this drive.
            // The Chaos OS guest accesses the drive directly via EPT MMIO.
            //
            Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_NOT_SUPPORTED;

        default:
            // All other majors (CREATE, CLOSE, CLEANUP, etc.) — pass through
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(ctx->LowerDeviceObject, Irp);
    }
}

// ── SymbioseNullPower ────────────────────────────────────────────────────────
// Pass power IRPs down the stack.
// Must call PoStartNextPowerIrp before passing down (harmless on Win8+).
// Reference: §III·6 (lines 1673-1681)
//
NTSTATUS
SymbioseNullPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp
    )
{
    NULL_FILTER_CONTEXT* ctx = (NULL_FILTER_CONTEXT*)DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(ctx->LowerDeviceObject, Irp);
}

// ── SymbioseNullPnp ──────────────────────────────────────────────────────────
// Pass PnP IRPs down the stack.
// On IRP_MN_REMOVE_DEVICE: pass IRP first, then detach and delete filter DO.
// Reference: §III·6 (lines 1687-1704)
//
NTSTATUS
SymbioseNullPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp
    )
{
    NULL_FILTER_CONTEXT* ctx   = (NULL_FILTER_CONTEXT*)DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION   stack = IoGetCurrentIrpStackLocation(Irp);

    if (stack->MinorFunction == IRP_MN_REMOVE_DEVICE) {
        // Pass the IRP down first, then detach and delete
        IoSkipCurrentIrpStackLocation(Irp);
        NTSTATUS status = IoCallDriver(ctx->LowerDeviceObject, Irp);
        IoDetachDevice(ctx->LowerDeviceObject);
        IoDeleteDevice(DeviceObject);
        return status;
    }

    // All other PnP minors — pass through without modification
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(ctx->LowerDeviceObject, Irp);
}
