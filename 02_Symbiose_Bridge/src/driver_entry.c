// // 02_Symbiose_Bridge/src/driver_entry.c
// Crucible: PATTERN-002 (expect() not unwrap()), PATTERN-007 (exit codes
// handled)

#include "symbiose_bridge.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, SymbioseDriverUnload)
#endif

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
            _In_ PUNICODE_STRING RegistryPath) {
  NTSTATUS status;
  WDF_DRIVER_CONFIG config;

  KdPrint(("SymbioseBridge: DriverEntry called\n"));

  WDF_DRIVER_CONFIG_INIT(&config, SymbioseDeviceAdd);

  // FIX 3: Changed config.DriverUnloading to config.EvtDriverUnload
  config.EvtDriverUnload = SymbioseDriverUnload;
  config.EvtDriverContextCleanup = NULL;

  status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,
                           &config, WDF_NO_HANDLE);

  if (!NT_SUCCESS(status)) {
    KdPrint(("SymbioseBridge: WdfDriverCreate failed with status 0x%08X\n",
             status));
    // PATTERN-002: Return error instead of crashing
    return status;
  }

  KdPrint(("SymbioseBridge: Driver initialized successfully\n"));
  return STATUS_SUCCESS;
}

VOID SymbioseDriverUnload(_In_ WDFDRIVER Driver) {
  PAGED_CODE();

  UNREFERENCED_PARAMETER(Driver);
  KdPrint(("SymbioseBridge: Driver unloading\n"));

  // Cleanup is handled by device context cleanup callback
  // PATTERN-015: All resources released in cleanup path
}
