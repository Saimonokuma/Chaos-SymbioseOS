#include "symbiose_bridge.h"
#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, SymbioseDriverUnload)
#endif

NTSTATUS SymbioseDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit) { return STATUS_SUCCESS; }
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) { return STATUS_SUCCESS; }
VOID SymbioseDriverUnload(_In_ WDFDRIVER Driver) { PAGED_CODE(); }

#ifndef _WIN32
int main() { return 0; }
#endif
