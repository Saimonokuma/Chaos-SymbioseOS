// // 03_HiveMind_Orchestrator/ChaosLoader/src/main.c
// Crucible: PATTERN-002 (expect not unwrap), PATTERN-005 (pathlib equivalent)

#include "../../../02_Symbiose_Bridge/inc/symbiose_ioctls.h" // FIX 23: Path corrected
#include "boot_params.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <stdint.h>
#include <string.h>
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
#include <winternl.h> // FIX 11: Required for NTSTATUS types in userspace

// PATTERN-005: Use environment variables, not hardcoded paths
#define SYMBIOSE_CORE_DIR L"%SystemDrive%\\Symbiose_Core"

// FIX 9: Corrected string escaping
#define SYMBIOSE_DRIVER_PATH L"\\\\.\\SymbioseBridge"

static NTSTATUS LoadFileIntoBuffer(_In_ LPCWSTR FilePath, _Out_ PVOID *Buffer,
                                   _Out_ SIZE_T *BufferSize) {
  HANDLE hFile = INVALID_HANDLE_VALUE;
  LARGE_INTEGER fileSize;
  PVOID fileBuffer = NULL;

  // PATTERN-002: Validate all parameters
  if (FilePath == NULL || Buffer == NULL || BufferSize == NULL) {
    return STATUS_INVALID_PARAMETER;
  }

  *Buffer = NULL;
  *BufferSize = 0;

  hFile = CreateFileW(FilePath, GENERIC_READ, FILE_SHARE_READ, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  if (hFile == INVALID_HANDLE_VALUE) {
    // FIX 10: Replaced kernel-only KdPrint with fwprintf
    fwprintf(stderr, L"ChaosLoader: Failed to open %ls (error %lu)\n", FilePath,
             GetLastError());
    return STATUS_NOT_FOUND;
  }

  // Get file size
  if (!GetFileSizeEx(hFile, &fileSize)) {
    fwprintf(stderr, L"ChaosLoader: Failed to get file size (error %lu)\n",
             GetLastError());
    CloseHandle(hFile);
    return STATUS_FILE_TOO_LARGE;
  }

  // Validate file size (max 1GB for kernel, 4GB for ramdisk)
  if (fileSize.QuadPart > 0x100000000ULL) {
    fwprintf(stderr, L"ChaosLoader: File too large: %lld bytes\n",
             fileSize.QuadPart);
    CloseHandle(hFile);
    return STATUS_FILE_TOO_LARGE;
  }

  // Allocate buffer
  fileBuffer = VirtualAlloc(NULL, (SIZE_T)fileSize.QuadPart,
                            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  if (fileBuffer == NULL) {
    fwprintf(stderr, L"ChaosLoader: Failed to allocate %lld bytes\n",
             fileSize.QuadPart);
    CloseHandle(hFile);
    return STATUS_NO_MEMORY;
  }

  // Read file
  DWORD bytesRead = 0;
  if (!ReadFile(hFile, fileBuffer, (DWORD)fileSize.QuadPart, &bytesRead,
                NULL)) {
    fwprintf(stderr, L"ChaosLoader: Failed to read file (error %lu)\n",
             GetLastError());
    VirtualFree(fileBuffer, 0, MEM_RELEASE);
    CloseHandle(hFile);
    return STATUS_IO_DEVICE_ERROR;
  }

  // PATTERN-015: Close handle immediately after use
  CloseHandle(hFile);

  *Buffer = fileBuffer;
  *BufferSize = (SIZE_T)bytesRead;

  return STATUS_SUCCESS;
}

int wmain(int argc, wchar_t *argv[]) {
  NTSTATUS status;
  HANDLE hDevice = INVALID_HANDLE_VALUE;
  PVOID kernelBuffer = NULL;
  SIZE_T kernelSize = 0;
  PVOID ramdiskBuffer = NULL;
  SIZE_T ramdiskSize = 0;
  BOOT_PARAMS bootParams = {0};
  DWORD bytesReturned = 0;

  UNREFERENCED_PARAMETER(argc);
  UNREFERENCED_PARAMETER(argv);

  fwprintf(stdout, L"ChaosLoader v0.1.0 - Chaos-Symbiose OS Loader\n");
  fwprintf(stdout, L"==============================================\n\n");

  // Step 1: Open the Symbiose Bridge driver
  hDevice = CreateFileW(SYMBIOSE_DRIVER_PATH, GENERIC_READ | GENERIC_WRITE, 0,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  if (hDevice == INVALID_HANDLE_VALUE) {
    fwprintf(stderr, L"❌ Failed to open Symbiose Bridge driver (error %lu)\n",
             GetLastError());
    fwprintf(stderr, L"   Ensure symbiose_bridge.sys is loaded.\n");
    return 1;
  }

  // Step 2: Load BZIMAGE
  WCHAR kernelPath[MAX_PATH];
  ExpandEnvironmentStringsW(SYMBIOSE_CORE_DIR L"\\BZIMAGE", kernelPath,
                            MAX_PATH);

  fwprintf(stdout, L"📂 Loading kernel: %ls\n", kernelPath);
  status = LoadFileIntoBuffer(kernelPath, &kernelBuffer, &kernelSize);
  if (!NT_SUCCESS(status)) {
    fwprintf(stderr, L"❌ Failed to load kernel (0x%08X)\n", status);
    goto cleanup;
  }
  fwprintf(stdout, L"   ✅ Kernel loaded: %zu bytes\n", kernelSize);

  // Step 3: Load CHAOS.RDZ
  WCHAR ramdiskPath[MAX_PATH];
  ExpandEnvironmentStringsW(SYMBIOSE_CORE_DIR L"\\CHAOS.RDZ", ramdiskPath,
                            MAX_PATH);

  fwprintf(stdout, L"📂 Loading ramdisk: %ls\n", ramdiskPath);
  status = LoadFileIntoBuffer(ramdiskPath, &ramdiskBuffer, &ramdiskSize);
  if (!NT_SUCCESS(status)) {
    fwprintf(stderr, L"❌ Failed to load ramdisk (0x%08X)\n", status);
    goto cleanup;
  }
  fwprintf(stdout, L"   ✅ Ramdisk loaded: %zu bytes\n", ramdiskSize);

  // Step 4: Initialize boot parameters
  // Fortification V: PID 1 Injection
  fwprintf(stdout, L"🔧 Initializing boot parameters\n");
  status = BootParams_Init(&bootParams, kernelBuffer, kernelSize, ramdiskBuffer,
                           ramdiskSize);
  if (!NT_SUCCESS(status)) {
    fwprintf(stderr, L"❌ Failed to initialize boot params (0x%08X)\n", status);
    goto cleanup;
  }

  // Inject init parameter: init=/symbiose/hive_mind
  status = BootParams_SetCommandLine(&bootParams, "init=/symbiose/hive_mind");
  if (!NT_SUCCESS(status)) {
    fwprintf(stderr, L"❌ Failed to set init parameter (0x%08X)\n", status);
    goto cleanup;
  }
  fwprintf(stdout, L"   ✅ PID 1 set to: /symbiose/hive_mind\n");

  // Step 5: Send kernel and ramdisk to driver
  fwprintf(stdout, L"🚀 Sending payload to Symbiose Bridge driver\n");

  SYMBIOSE_PAYLOAD payload = {0};
  payload.KernelBuffer = kernelBuffer;
  payload.KernelSize = kernelSize;
  payload.RamdiskBuffer = ramdiskBuffer;
  payload.RamdiskSize = ramdiskSize;
  payload.BootParams = &bootParams;

  if (!DeviceIoControl(hDevice, IOCTL_SYMBIOSE_SEND_PAYLOAD, &payload,
                       sizeof(payload), NULL, 0, &bytesReturned, NULL)) {
    fwprintf(stderr, L"❌ IOCTL failed (error %lu)\n", GetLastError());
    goto cleanup;
  }

  fwprintf(stdout, L"✅ Payload sent successfully\n");
  fwprintf(stdout, L"⚠️  Chaos-OS is now running. Windows is suspended.\n");

cleanup:
  // PATTERN-015: Always cleanup
  if (kernelBuffer != NULL) {
    VirtualFree(kernelBuffer, 0, MEM_RELEASE);
  }
  if (ramdiskBuffer != NULL) {
    VirtualFree(ramdiskBuffer, 0, MEM_RELEASE);
  }
  if (hDevice != INVALID_HANDLE_VALUE) {
    CloseHandle(hDevice);
  }

  return NT_SUCCESS(status) ? 0 : 1;
}
