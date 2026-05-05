#ifndef SYMBIOSE_IOCTLS_H
#define SYMBIOSE_IOCTLS_H

#ifdef _WIN32
#include <windows.h>
#else
#include <stdint.h>
#define FILE_DEVICE_UNKNOWN 0x00000022
#define METHOD_BUFFERED 0
#define FILE_ANY_ACCESS 0
#define CTL_CODE(DeviceType, Function, Method, Access)                         \
  (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
#endif

#include "boot_params.h"

#define IOCTL_SYMBIOSE_SEND_PAYLOAD                                            \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _SYMBIOSE_PAYLOAD {
  PVOID KernelBuffer;
  SIZE_T KernelSize;
  PVOID RamdiskBuffer;
  SIZE_T RamdiskSize;
  PBOOT_PARAMS BootParams;
} SYMBIOSE_PAYLOAD, *PSYMBIOSE_PAYLOAD;

#endif // SYMBIOSE_IOCTLS_H
