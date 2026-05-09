/*++
 * driver_entry.c — SymbioseOS KMDF Ring-0 Hypervisor Driver Entry Point
 *
 * BRIDGE-000: WPP Tracing initialization (WPP_INIT_TRACING / WPP_CLEANUP)
 * BRIDGE-001: DriverEntry + WdfDriverCreate + EvtDriverDeviceAdd registration
 *
 * Reference: Interactive_Plan.md §III·1 (lines 798-828)
 *
 * Constitutional constraints enforced:
 *   X·1  — NO WHPX (#include <WinHvPlatform.h> is FORBIDDEN)
 *   X·2  — PPO assertion before WdfDeviceCreate (handled in symbiose_bridge.c)
 *   X·3  — Spinlocks released before WdfRequestComplete
 *
 * WPP Tracing MUST be initialized FIRST in DriverEntry — before any
 * TraceEvents call. WPP_CLEANUP is called in EvtDriverUnload and
 * on all DriverEntry failure paths.
 *--*/

#include <ntddk.h>
#include <wdf.h>

#include "trace.h"
#include "driver_entry.tmh"   // WPP auto-generated — MUST follow trace.h

//
// Forward declarations
//
EVT_WDF_DRIVER_DEVICE_ADD   EvtDriverDeviceAdd;    // Defined in symbiose_bridge.c
EVT_WDF_DRIVER_UNLOAD       EvtDriverUnload;

//
// DriverEntry — Driver initialization entry point
//
// Called by the OS when symbiose_bridge.sys is loaded.
// Initialization sequence (§III·1):
//   1. WPP_INIT_TRACING — MUST be first
//   2. WDF_DRIVER_CONFIG — register EvtDriverDeviceAdd
//   3. WdfDriverCreate — create the WDF driver object
//
// On failure: WPP_CLEANUP is called before returning the error status.
//
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG   config;
    NTSTATUS            status;

    //
    // 1. WPP tracing MUST be initialized FIRST — before any TraceEvents call
    //    Reference: §III·1, WDK docs: "Adding WPP Software Tracing to a Windows Driver"
    //
    WPP_INIT_TRACING(DriverObject, RegistryPath);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "SymbioseOS KMDF Hypervisor v3.0 — DriverEntry");

    //
    // 2. Configure WDF driver object
    //    Register EvtDriverDeviceAdd callback (defined in symbiose_bridge.c)
    //    Register EvtDriverUnload for WPP cleanup
    //
    WDF_DRIVER_CONFIG_INIT(&config, EvtDriverDeviceAdd);
    config.EvtDriverUnload = EvtDriverUnload;

    //
    // 3. Create the WDF driver object
    //
    status = WdfDriverCreate(
                DriverObject,
                RegistryPath,
                WDF_NO_OBJECT_ATTRIBUTES,
                &config,
                WDF_NO_HANDLE
                );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER,
                    "WdfDriverCreate failed: %!STATUS!", status);

        //
        // Clean up WPP on failure path — EvtDriverUnload will NOT be called
        // because WdfDriverCreate failed (the WDFDRIVER object doesn't exist).
        //
        WPP_CLEANUP(DriverObject);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "WdfDriverCreate succeeded — waiting for PnP device arrival");

    return STATUS_SUCCESS;
}

//
// EvtDriverUnload — Driver unload callback
//
// Called by WDF when the driver is being unloaded.
// Must call WPP_CLEANUP to deactivate software tracing.
//
// Reference: §III·1 (line 825-827)
//
VOID
EvtDriverUnload(
    _In_ WDFDRIVER Driver
    )
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "EvtDriverUnload — cleaning up WPP tracing");

    //
    // Deactivate WPP tracing
    // WdfDriverWdmGetDriverObject extracts the WDM DRIVER_OBJECT from the
    // WDF handle, which is what WPP_CLEANUP requires.
    //
    WPP_CLEANUP(WdfDriverWdmGetDriverObject(Driver));
}
