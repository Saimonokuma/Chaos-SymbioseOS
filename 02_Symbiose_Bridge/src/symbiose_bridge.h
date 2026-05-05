// // 02_Symbiose_Bridge/src/symbiose_bridge.h
// Crucible: PATTERN-002 (no unwrap in prod), PATTERN-006 (no bare except)

#ifndef SYMBIOSE_BRIDGE_H
#define SYMBIOSE_BRIDGE_H

#include "../inc/symbiose_ioctls.h" // FIX 23: Include shared IOCTL definitions
#ifdef _WIN32
#include <ntddk.h>
// removed
// removed
#else
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef void* PVOID;
typedef size_t SIZE_T;
typedef char CHAR;
typedef unsigned char BOOLEAN;
typedef const char* PCSTR;
typedef int NTSTATUS;
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER -1
#define _Out_
#define _In_
#define _Inout_
#define FALSE 0
#define TRUE 1
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
#endif
// dummy ntddk
// removed
// removed

// ============================================================
// Constants & Configuration
// ============================================================

#define SYMBIOSE_DEVICE_NAME L"\\Device\\SymbioseBridge"
#define SYMBIOSE_DOS_NAME L"\\DosDevices\\SymbioseBridge"

// FIX 2: Replaced non-existent MAKE_ULONGLONG macro with explicit ULL
#define SYMBIOSE_DRIVER_VERSION 0x0000000100000000ULL

// ACPI Power State Callbacks
#define SYMBIOSE_ACPI_NOTIFY_SHUTDOWN 0x01
#define SYMBIOSE_ACPI_NOTIFY_SUSPEND 0x02
#define SYMBIOSE_ACPI_NOTIFY_RESUME 0x03

// Timeout for LLM ACK (milliseconds)
#define SYMBIOSE_ACK_TIMEOUT_MS 30000 // 30 seconds

// Maximum number of isolated NVMe devices
#define SYMBIOSE_MAX_NVME_DEVICES 4

// ============================================================
// Data Structures
// ============================================================

typedef enum _SYMBIOSE_STATE {
  SymbioseStateUninitialized = 0,
  SymbioseStateIdle,
  SymbioseStateChaosRunning,
  SymbioseStateShutdownPending,
  SymbioseStateShutdownComplete,
  SymbioseStateError
} SYMBIOSE_STATE;

typedef struct _SYMBIOSE_NVME_ISOLATION {
  ULONG DeviceId;            // PCI device ID
  ULONG VendorId;            // PCI vendor ID
  BOOLEAN Isolated;          // TRUE if Windows NTFS driver detached
  UNICODE_STRING DevicePath; // NT device path
} SYMBIOSE_NVME_ISOLATION, *PSYMBIOSE_NVME_ISOLATION;

typedef struct _SYMBIOSE_DEVICE_CONTEXT {
  SYMBIOSE_STATE State;

  // ACPI callback registration
  PVOID AcpiNotificationHandle;

  // NVMe isolation tracking
  SYMBIOSE_NVME_ISOLATION IsolatedDevices[SYMBIOSE_MAX_NVME_DEVICES];
  ULONG IsolatedDeviceCount;

  // LLM communication
  KEVENT ShutdownEvent; // Signaled when Windows shutdown detected
  KEVENT AckEvent;      // Signaled when LLM sends ACK_READY_TO_DIE
  KTIMER AckTimer;      // Timeout timer for ACK

  // Chaos-OS kernel state
  PVOID ChaosKernelBuffer; // BZIMAGE loaded into non-paged pool
  SIZE_T ChaosKernelSize;
  PVOID ChaosRamdiskBuffer; // CHAOS.RDZ loaded into non-paged pool
  SIZE_T ChaosRamdiskSize;

  // Synchronization
  WDFWAITLOCK StateLock; // Protects State transitions

} SYMBIOSE_DEVICE_CONTEXT, *PSYMBIOSE_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SYMBIOSE_DEVICE_CONTEXT,
                                   SymbioseDeviceGetContext)

// ============================================================
// Function Declarations
// ============================================================

// Driver entry and cleanup
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_UNLOAD SymbioseDriverUnload;

// Device creation
EVT_WDF_DEVICE_ADD SymbioseDeviceAdd;
EVT_WDF_DEVICE_CONTEXT_CLEANUP SymbioseDeviceCleanup;

// ACPI notification callback
ACPI_NOTIFICATION_HANDLER SymbioseAcpiNotificationCallback;

// IOCTL handlers
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL SymbioseIoctlHandler;

// NVMe isolation
NTSTATUS SymbioseIsolateNvmeDevice(_In_ WDFDEVICE Device, _In_ ULONG VendorId,
                                   _In_ ULONG DeviceId);

NTSTATUS SymbioseRestoreNvmeDevice(_In_ WDFDEVICE Device,
                                   _In_ ULONG DeviceIndex);

// Helpers for PCI matching
NTSTATUS SymbioseFindPciDevice(ULONG VendorId, ULONG DeviceId,
                               PUNICODE_STRING DevicePath);
NTSTATUS SymbioseDetachDriverStack(PUNICODE_STRING DevicePath);
NTSTATUS SymbioseReattachDriverStack(PUNICODE_STRING DevicePath);
NTSTATUS SymbioseLoadNullDriver(PUNICODE_STRING DevicePath);
NTSTATUS SymbioseUnloadNullDriver(PUNICODE_STRING DevicePath);

// Assembly thunk interface (defined in SwitchToChaos.asm)
NTSTATUS SwitchToChaosKernel(_In_ PVOID KernelImage, _In_ SIZE_T KernelSize,
                             _In_ PVOID RamdiskImage, _In_ SIZE_T RamdiskSize,
                             _In_ PVOID BootParams);

#endif // SYMBIOSE_BRIDGE_H
