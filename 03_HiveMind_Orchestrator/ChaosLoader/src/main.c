// 03_HiveMind_Orchestrator/ChaosLoader/src/main.c
// Crucible: PATTERN-002 (expect not unwrap), PATTERN-005 (pathlib equivalent)

#ifdef _WIN32
#include <windows.h>
#include <winternl.h> // FIX 11: Required for NTSTATUS types in userspace
#else
#include <stdint.h>
#include <string.h>
typedef void *HANDLE;
typedef void *PVOID;
typedef size_t SIZE_T;
typedef const char *LPCWSTR;
typedef int NTSTATUS;

typedef union _LARGE_INTEGER {
  struct {
    unsigned int LowPart;
    int HighPart;
  } u;
  long long QuadPart;
} LARGE_INTEGER;

typedef unsigned long DWORD;
typedef unsigned short WCHAR;

#define STATUS_UNSUCCESSFUL -1
#define STATUS_NOT_FOUND -3
#define STATUS_FILE_TOO_LARGE -4
#define STATUS_NO_MEMORY -5
#define STATUS_IO_DEVICE_ERROR -6
#define STATUS_SUCCESS 0

#define INVALID_HANDLE_VALUE ((void *)-1)
#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define FILE_SHARE_READ 0x00000001
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define PAGE_READWRITE 0x04
#define MEM_RELEASE 0x8000
#define MAX_PATH 260
#define UNREFERENCED_PARAMETER(P) (void)(P)

HANDLE CreateFileW(const void *lpFileName, DWORD dwDesiredAccess,
                   DWORD dwShareMode, void *lpSecurityAttributes,
                   DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
                   HANDLE hTemplateFile) {
  return INVALID_HANDLE_VALUE;
}
int GetFileSizeEx(HANDLE hFile, LARGE_INTEGER *lpFileSize) { return 0; }
void *VirtualAlloc(void *lpAddress, SIZE_T dwSize, DWORD flAllocationType,
                   DWORD flProtect) {
  return NULL;
}
int ReadFile(HANDLE hFile, void *lpBuffer, DWORD nNumberOfBytesToRead,
             DWORD *lpNumberOfBytesRead, void *lpOverlapped) {
  return 0;
}
void VirtualFree(void *lpAddress, SIZE_T dwSize, DWORD dwFreeType) {}
void CloseHandle(HANDLE hObject) {}
DWORD GetLastError() { return 0; }
int ExpandEnvironmentStringsW(const void *lpSrc, WCHAR *lpDst, DWORD nSize) {
  return 0;
}
int DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, void *lpInBuffer,
                    DWORD nInBufferSize, void *lpOutBuffer,
                    DWORD nOutBufferSize, DWORD *lpBytesReturned,
                    void *lpOverlapped) {
  return 0;
}
void KdPrint(const char *x) {}
#endif

#include "boot_params.h"
#include "symbiose_ioctls.h"
#include <stdio.h>
#include <stdlib.h>

// PATTERN-005: Use environment variables, not hardcoded paths
#define SYMBIOSE_CORE_DIR L"%SystemDrive%\\Symbiose_Core"
#define SYMBIOSE_DRIVER_NAME L"SymbioseBridge"

// FIX 9: Corrected string escaping
#define SYMBIOSE_DRIVER_PATH L"\\\\.\\SymbioseBridge"

static NTSTATUS LoadFileIntoBuffer(_In_ LPCWSTR FilePath, _Out_ PVOID *Buffer,
                                   _Out_ SIZE_T *BufferSize) {
  HANDLE hFile = INVALID_HANDLE_VALUE;
  LARGE_INTEGER fileSize;
  PVOID fileBuffer = NULL;
  NTSTATUS status = STATUS_UNSUCCESSFUL;

  // PATTERN-002: Validate all parameters
  if (FilePath == NULL || Buffer == NULL || BufferSize == NULL) {
    return STATUS_INVALID_PARAMETER;
  }

  *Buffer = NULL;
  *BufferSize = 0;

  hFile = CreateFileW(FilePath, GENERIC_READ, FILE_SHARE_READ, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  if (hFile == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "ChaosLoader: Failed to open (error %lu)\n",
             GetLastError());
    return STATUS_NOT_FOUND;
  }

  // Get file size
  if (!GetFileSizeEx(hFile, &fileSize)) {
    fprintf(stderr, "ChaosLoader: Failed to get file size (error %lu)\n",
             GetLastError());
    CloseHandle(hFile);
    return STATUS_FILE_TOO_LARGE;
  }

  // Validate file size (max 1GB for kernel, 4GB for ramdisk)
  if (fileSize.QuadPart > 0x100000000ULL) {
    fprintf(stderr, "ChaosLoader: File too large (error %lu)\n",
             GetLastError());
    CloseHandle(hFile);
    return STATUS_FILE_TOO_LARGE;
  }

  // Allocate buffer
  fileBuffer = VirtualAlloc(NULL, (SIZE_T)fileSize.QuadPart,
                            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  if (fileBuffer == NULL) {
    fprintf(stderr, "ChaosLoader: Failed to allocate bytes (error %lu)\n",
             GetLastError());
    CloseHandle(hFile);
    return STATUS_NO_MEMORY;
  }

  // Read file
  DWORD bytesRead = 0;
  if (!ReadFile(hFile, fileBuffer, (DWORD)fileSize.QuadPart, &bytesRead,
                NULL)) {
    fprintf(stderr, "ChaosLoader: Failed to read file (error %lu)\n",
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

#ifdef _WIN32
int wmain(int argc, wchar_t *argv[])
#else
int main(int argc, char *argv[])
#endif
{
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

  printf("ChaosLoader v0.1.0 - Chaos-Symbiose OS Loader\n");
  printf("==============================================\n\n");

  // Step 1: Open the Symbiose Bridge driver
  hDevice = CreateFileW(SYMBIOSE_DRIVER_PATH, GENERIC_READ | GENERIC_WRITE, 0,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  if (hDevice == INVALID_HANDLE_VALUE) {
    printf("❌ Failed to open Symbiose Bridge driver\n");
    printf("	 Ensure symbiose_bridge.sys is loaded.\n");
    return 1;
  }

  // Step 2: Load BZIMAGE
  WCHAR kernelPath[MAX_PATH];
  ExpandEnvironmentStringsW(SYMBIOSE_CORE_DIR L"\\BZIMAGE", kernelPath,
                            MAX_PATH);

  printf("📂 Loading kernel\n");
  status =
      LoadFileIntoBuffer((const void *)kernelPath, &kernelBuffer, &kernelSize);
  if (!NT_SUCCESS(status)) {
    printf("❌ Failed to load kernel (0x%08X)\n", status);
    goto cleanup;
  }
  printf("	 ✅ Kernel loaded: %zu bytes\n", kernelSize);

  // Step 3: Load CHAOS.RDZ
  WCHAR ramdiskPath[MAX_PATH];
  ExpandEnvironmentStringsW(SYMBIOSE_CORE_DIR L"\\CHAOS.RDZ", ramdiskPath,
                            MAX_PATH);

  printf("📂 Loading ramdisk\n");
  status = LoadFileIntoBuffer((const void *)ramdiskPath, &ramdiskBuffer,
                              &ramdiskSize);
  if (!NT_SUCCESS(status)) {
    printf("❌ Failed to load ramdisk (0x%08X)\n", status);
    goto cleanup;
  }
  printf("	 ✅ Ramdisk loaded: %zu bytes\n", ramdiskSize);

  // Step 4: Initialize boot parameters
  // Fortification V: PID 1 Injection
  printf("🔧 Initializing boot parameters\n");
  status = BootParams_Init(&bootParams, kernelBuffer, kernelSize, ramdiskBuffer,
                           ramdiskSize);
  if (!NT_SUCCESS(status)) {
    printf("❌ Failed to initialize boot params (0x%08X)\n", status);
    goto cleanup;
  }

  // Inject init parameter: init=/symbiose/hive_mind
  status = BootParams_SetCommandLine(&bootParams, "init=/symbiose/hive_mind");
  if (!NT_SUCCESS(status)) {
    printf("❌ Failed to set init parameter (0x%08X)\n", status);
    goto cleanup;
  }
  printf("	 ✅ PID 1 set to: /symbiose/hive_mind\n");

  // Step 5: Send kernel and ramdisk to driver
  printf("🚀 Sending payload to Symbiose Bridge driver\n");

  SYMBIOSE_PAYLOAD payload = {0};
  payload.KernelBuffer = kernelBuffer;
  payload.KernelSize = kernelSize;
  payload.RamdiskBuffer = ramdiskBuffer;
  payload.RamdiskSize = ramdiskSize;
  payload.BootParams = &bootParams;

  if (!DeviceIoControl(hDevice, IOCTL_SYMBIOSE_SEND_PAYLOAD, &payload,
                       sizeof(payload), NULL, 0, &bytesReturned, NULL)) {
    printf("❌ IOCTL failed\n");
    goto cleanup;
  }

  printf("✅ Payload sent successfully\n");
  printf("⚠️  Chaos-OS is now running. Windows is suspended.\n");

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
