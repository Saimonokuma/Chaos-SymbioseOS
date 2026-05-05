#include "boot_params.h"

NTSTATUS BootParams_Init(PBOOT_PARAMS BootParams, PVOID KernelBuffer, SIZE_T KernelSize, PVOID RamdiskBuffer, SIZE_T RamdiskSize) {
    if (!BootParams) return STATUS_INVALID_PARAMETER;
    BootParams->KernelBuffer = KernelBuffer;
    BootParams->KernelSize = KernelSize;
    BootParams->RamdiskBuffer = RamdiskBuffer;
    BootParams->RamdiskSize = RamdiskSize;
    BootParams->CommandLineLen = 0;
    BootParams->CommandLineSet = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS BootParams_SetCommandLine(PBOOT_PARAMS BootParams, PCSTR CommandLine) {
    if (!BootParams || !CommandLine) return STATUS_INVALID_PARAMETER;
    strncpy(BootParams->CommandLine, CommandLine, MAX_CMDLINE_LEN - 1);
    BootParams->CommandLine[MAX_CMDLINE_LEN - 1] = '\0';
    BootParams->CommandLineLen = strlen(BootParams->CommandLine);
    BootParams->CommandLineSet = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS BootParams_PatchKernelZeroPage(PBOOT_PARAMS BootParams) {
    return STATUS_SUCCESS;
}
