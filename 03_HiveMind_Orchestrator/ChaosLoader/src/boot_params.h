// 03_HiveMind_Orchestrator/ChaosLoader/src/boot_params.h
// Crucible: PATTERN-002 (validate all inputs)

#ifndef BOOT_PARAMS_H
#define BOOT_PARAMS_H

#ifdef _WIN32
#include <windows.h>
#else
#include <stdint.h>
#include <string.h>
typedef void* PVOID;
typedef size_t SIZE_T;
typedef char CHAR;
typedef const char* PCSTR;
typedef unsigned char BOOLEAN;
typedef int NTSTATUS;
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER -1
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
#define TRUE 1
#define FALSE 0
#define _Out_
#define _In_
#define _Inout_
#endif

// Linux x86 boot protocol constants
#define BOOT_PARAMS_MAGIC	 0xAA55
#define LINUX_BOOT_HDR_MAGIC 0x53726448	 // "HdrS"
#define LINUX_BOOT_HDR_VERSION 0x020F	 // Version 2.15

// Maximum command line length
#define MAX_CMDLINE_LEN		 2048

// Linux setup header offsets (from setup.S)
#pragma pack(push, 1)
typedef struct _LINUX_SETUP_HEADER {
	uint8_t  setup_sects;			 // 0x1F1
	uint16_t root_flags;			 // 0x1F2
	uint16_t syssize;				 // 0x1F4 (4 bytes in 2.08+)
	uint16_t ram_size;			 // 0x1F8
	uint16_t vid_mode;			 // 0x1FA
	uint16_t root_dev;			 // 0x1FC
	uint8_t  signature[2];		// 0x1FE (0xAA55)
	uint8_t  jump[2];				// 0x200
	uint8_t  header_magic[4];		// 0x202 ("HdrS")
	uint16_t protocol_version;	// 0x206
	uint32_t cmdline_size;		// 0x238
	uint32_t cmdline_ptr;			// 0x228
	// ... many more fields
} LINUX_SETUP_HEADER, *PLINUX_SETUP_HEADER;
#pragma pack(pop)

typedef struct _BOOT_PARAMS {
	PVOID KernelBuffer;
	SIZE_T KernelSize;
	PVOID RamdiskBuffer;
	SIZE_T RamdiskSize;
	CHAR CommandLine[MAX_CMDLINE_LEN];
	SIZE_T CommandLineLen;
	BOOLEAN CommandLineSet;
} BOOT_PARAMS, *PBOOT_PARAMS;

NTSTATUS BootParams_Init(
	_Out_ PBOOT_PARAMS BootParams,
	_In_ PVOID KernelBuffer,
	_In_ SIZE_T KernelSize,
	_In_ PVOID RamdiskBuffer,
	_In_ SIZE_T RamdiskSize
);

NTSTATUS BootParams_SetCommandLine(
	_Inout_ PBOOT_PARAMS BootParams,
	_In_ PCSTR CommandLine
);

NTSTATUS BootParams_PatchKernelZeroPage(
	_Inout_ PBOOT_PARAMS BootParams
);

#endif // BOOT_PARAMS_H
