// 02_Symbiose_Bridge/src/symbiose_bridge.h
#ifndef SYMBIOSE_BRIDGE_H
#define SYMBIOSE_BRIDGE_H

#ifdef _WIN32
#include <ntddk.h>
#include <wdf.h>
#include <wdm.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NTSTATUS int
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER -1
#define STATUS_INSUFFICIENT_RESOURCES -2
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)

#define _In_
#define _Inout_
#define _Out_
#define ALLOC_PRAGMA
#define INIT
#define PAGE
#define PAGED_CODE()
#define UNREFERENCED_PARAMETER(P) (void)(P)
#define KdPrint(A)

typedef void *PVOID;
typedef void *HANDLE;
typedef void *WDFDRIVER;
typedef void *WDFDEVICE;
typedef void *PWDFDEVICE_INIT;
typedef void *PDRIVER_OBJECT;
typedef void *WDFWAITLOCK;
typedef void *WDF_OBJECT_ATTRIBUTES;
typedef unsigned long ULONG;
typedef unsigned char BOOLEAN;
typedef unsigned long long ULONGLONG;
typedef size_t SIZE_T;
typedef void VOID;

typedef struct _WDF_DRIVER_CONFIG {
  void *EvtDriverDeviceAdd;
  void *DriverUnloading;
  void *EvtDriverContextCleanup;
} WDF_DRIVER_CONFIG, *PWDF_DRIVER_CONFIG;

#define MAKE_ULONGLONG(a, b, c)                                                \
  (((ULONGLONG)(a) << 32) | ((ULONGLONG)(b) << 16) | (c))
#define TRUE 1
#define FALSE 0

#define FILE_DEVICE_UNKNOWN 0x00000022
#define METHOD_BUFFERED 0
#define FILE_ANY_ACCESS 0
#define CTL_CODE(DeviceType, Function, Method, Access)                         \
  (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))

#define WDF_NO_OBJECT_ATTRIBUTES NULL
#define WDF_NO_HANDLE NULL

#define WDF_DRIVER_CONFIG_INIT(config, initFunc)                               \
  do {                                                                         \
    (config)->EvtDriverDeviceAdd = initFunc;                                   \
  } while (0)
#define WdfDriverCreate(DriverObject, RegistryPath, Attributes, Config,        \
                        Handle)                                                \
  STATUS_SUCCESS

typedef struct _UNICODE_STRING {
  unsigned short Length;
  unsigned short MaximumLength;
  unsigned short *Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _KEVENT {
  int dummy;
} KEVENT;

typedef struct _KTIMER {
  int dummy;
} KTIMER;

typedef union _LARGE_INTEGER {
  struct {
    unsigned int LowPart;
    int HighPart;
  } u;
  long long QuadPart;
} LARGE_INTEGER;

#define IO_NO_INCREMENT 0
#define KeSetEvent(Event, Increment, Wait)                                     \
  do {                                                                         \
  } while (0)
#define WDF_REL_TIMEOUT_IN_MS(ms) (-(long long)((ms) * 10000))
#define KeSetTimer(Timer, DueTime, Dpc)                                        \
  do {                                                                         \
  } while (0)
#define WdfWaitLockAcquire(Lock, Timeout)                                      \
  do {                                                                         \
  } while (0)
#define WdfWaitLockRelease(Lock)                                               \
  do {                                                                         \
  } while (0)
#define RtlFreeUnicodeString(Str)                                              \
  do {                                                                         \
  } while (0)

#endif // _WIN32

// ============================================================
// Constants & Configuration
// ============================================================

#define SYMBIOSE_DEVICE_NAME L"\\Device\\SymbioseBridge"
#define SYMBIOSE_DOS_NAME L"\\DosDevices\\SymbioseBridge"
#define SYMBIOSE_DRIVER_VERSION MAKE_ULONGLONG(0, 1, 0) // 0.1.0

#define SYMBIOSE_ACPI_NOTIFY_SHUTDOWN 0x01
#define SYMBIOSE_ACPI_NOTIFY_SUSPEND 0x02
#define SYMBIOSE_ACPI_NOTIFY_RESUME 0x03

#define IOCTL_SYMBIOSE_SEND_SHUTDOWN                                           \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_RECV_ACK                                                \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_GET_STATUS                                              \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_SET_NVME_ISOLATION                                      \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define SYMBIOSE_ACK_TIMEOUT_MS 30000

#define SYMBIOSE_MAX_NVME_DEVICES 4

typedef enum _SYMBIOSE_STATE {
  SymbioseStateUninitialized = 0,
  SymbioseStateIdle,
  SymbioseStateChaosRunning,
  SymbioseStateShutdownPending,
  SymbioseStateShutdownComplete,
  SymbioseStateError
} SYMBIOSE_STATE;

typedef struct _SYMBIOSE_NVME_ISOLATION {
  ULONG DeviceId;
  ULONG VendorId;
  BOOLEAN Isolated;
  UNICODE_STRING DevicePath;
} SYMBIOSE_NVME_ISOLATION, *PSYMBIOSE_NVME_ISOLATION;

typedef struct _SYMBIOSE_DEVICE_CONTEXT {
  SYMBIOSE_STATE State;
  PVOID AcpiNotificationHandle;
  SYMBIOSE_NVME_ISOLATION IsolatedDevices[SYMBIOSE_MAX_NVME_DEVICES];
  ULONG IsolatedDeviceCount;
  KEVENT ShutdownEvent;
  KEVENT AckEvent;
  KTIMER AckTimer;
  PVOID ChaosKernelBuffer;
  SIZE_T ChaosKernelSize;
  PVOID ChaosRamdiskBuffer;
  SIZE_T ChaosRamdiskSize;
  WDFWAITLOCK StateLock;
} SYMBIOSE_DEVICE_CONTEXT, *PSYMBIOSE_DEVICE_CONTEXT;

#ifndef _WIN32
#define WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(type, name)                         \
  type *name(WDFDEVICE device) {                                               \
    static type ctx;                                                           \
    return &ctx;                                                               \
  }
#endif

#ifdef _WIN32
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SYMBIOSE_DEVICE_CONTEXT,
                                   SymbioseDeviceGetContext)
#endif

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
void SymbioseDriverUnload(WDFDRIVER Driver);

NTSTATUS SymbioseDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit);
void SymbioseDeviceCleanup(WDFDEVICE Device);

void SymbioseAcpiNotificationCallback(PVOID Context, ULONG NotifyCode);
void SymbioseIoctlHandler(void *Queue, void *Request, size_t OutputBufferLength,
                          size_t InputBufferLength, ULONG IoControlCode);

NTSTATUS SymbioseIsolateNvmeDevice(_In_ WDFDEVICE Device, _In_ ULONG VendorId,
                                   _In_ ULONG DeviceId);
NTSTATUS SymbioseRestoreNvmeDevice(_In_ WDFDEVICE Device,
                                   _In_ ULONG DeviceIndex);
NTSTATUS SwitchToChaosKernel(_In_ PVOID KernelImage, _In_ SIZE_T KernelSize,
                             _In_ PVOID RamdiskImage, _In_ SIZE_T RamdiskSize,
                             _In_ PVOID BootParams);
NTSTATUS SymbioseInjectInitParameter(_In_ PVOID BootParams,
                                     _In_ const char *InitPath);
NTSTATUS SymbioseFindPciDevice(ULONG VendorId, ULONG DeviceId,
                               PUNICODE_STRING DevicePath);
NTSTATUS SymbioseDetachDriverStack(PUNICODE_STRING DevicePath);
NTSTATUS SymbioseLoadNullDriver(PUNICODE_STRING DevicePath);
NTSTATUS SymbioseUnloadNullDriver(PUNICODE_STRING DevicePath);
NTSTATUS SymbioseReattachDriverStack(PUNICODE_STRING DevicePath);

#endif // SYMBIOSE_BRIDGE_H
