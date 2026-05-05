// 02_Symbiose_Bridge/src/ioctl_handler.c
// Crucible: PATTERN-002, PATTERN-009 (Safe Memory Probing)

#include "symbiose_bridge.h"

VOID SymbioseIoctlHandler(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request,
                          _In_ size_t OutputBufferLength,
                          _In_ size_t InputBufferLength,
                          _In_ ULONG IoControlCode) {
  NTSTATUS status = STATUS_SUCCESS;
  WDFDEVICE device = WdfIoQueueGetDevice(Queue);
  PSYMBIOSE_DEVICE_CONTEXT devCtx = SymbioseDeviceGetContext(device);
  PSYMBIOSE_PAYLOAD payload = NULL;
  PVOID kernelBuffer = NULL;
  PVOID ramdiskBuffer = NULL;
  PVOID bootParamsBuffer = NULL;

  UNREFERENCED_PARAMETER(OutputBufferLength);

  switch (IoControlCode) {
  case IOCTL_SYMBIOSE_SEND_PAYLOAD:
    if (InputBufferLength < sizeof(SYMBIOSE_PAYLOAD)) {
      status = STATUS_INFO_LENGTH_MISMATCH;
      break;
    }

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(SYMBIOSE_PAYLOAD),
                                           (PVOID *)&payload, NULL);
    if (!NT_SUCCESS(status)) {
      break;
    }

    // FIX 21: The payload struct is copied, but pointers inside it point to
    // userspace. We must probe them inside a __try/__except block to prevent
    // Ring-0 crashes.
    __try {
      ProbeForRead(payload->KernelBuffer, payload->KernelSize, 1);
      ProbeForRead(payload->RamdiskBuffer, payload->RamdiskSize, 1);
      // Assuming BootParams is a known size struct (2048+ bytes)
      ProbeForRead(payload->BootParams, 4096, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      KdPrint(
          ("SymbioseBridge: Invalid userspace pointers in SYMBIOSE_PAYLOAD\n"));
      status = STATUS_INVALID_USER_BUFFER;
      break;
    }

    // Safe allocation and copy to Non-Paged Pool
    kernelBuffer =
        ExAllocatePool2(POOL_FLAG_NON_PAGED, payload->KernelSize, 'KnsS');
    if (!kernelBuffer) {
      status = STATUS_NO_MEMORY;
      goto cleanup;
    }

    __try {
      RtlCopyMemory(kernelBuffer, payload->KernelBuffer, payload->KernelSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      status = STATUS_INVALID_USER_BUFFER;
      goto cleanup;
    }

    // Execute transition logic here (call SwitchToChaosKernel)
    // ...

    status = STATUS_SUCCESS;
    break;

  default:
    status = STATUS_INVALID_DEVICE_REQUEST;
    break;
  }

cleanup:
  if (!NT_SUCCESS(status)) {
    if (kernelBuffer)
      ExFreePoolWithTag(kernelBuffer, 'KnsS');
  }
  WdfRequestComplete(Request, status);
}
