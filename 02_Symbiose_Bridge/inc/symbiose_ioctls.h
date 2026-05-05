// // 02_Symbiose_Bridge/inc/symbiose_ioctls.h
// FIX 23: Shared definitions between Ring-0 driver and Ring-3 ChaosLoader

#ifndef SYMBIOSE_IOCTLS_H
#define SYMBIOSE_IOCTLS_H

#ifdef _WIN32
#include <windows.h>
#endif

// LLM Communication IOCTLs
#define IOCTL_SYMBIOSE_SEND_SHUTDOWN                                           \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_RECV_ACK                                                \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_GET_STATUS                                              \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_SET_NVME_ISOLATION                                      \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_SEND_PAYLOAD                                            \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

// FIX 12: Explicitly defined SYMBIOSE_PAYLOAD
typedef struct _SYMBIOSE_PAYLOAD {
  PVOID KernelBuffer;
  SIZE_T KernelSize;
  PVOID RamdiskBuffer;
  SIZE_T RamdiskSize;
  PVOID BootParams;
} SYMBIOSE_PAYLOAD, *PSYMBIOSE_PAYLOAD;

#endif // SYMBIOSE_IOCTLS_H
