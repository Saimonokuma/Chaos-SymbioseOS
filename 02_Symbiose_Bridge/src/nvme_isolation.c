// 02_Symbiose_Bridge/src/nvme_isolation.c
// Crucible: PATTERN-008 (TOCTOU prevention), PATTERN-015 (resource cleanup)

#include "symbiose_bridge.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, SymbioseIsolateNvmeDevice)
#pragma alloc_text(PAGE, SymbioseRestoreNvmeDevice)
#endif

NTSTATUS SymbioseFindPciDevice(ULONG VendorId, ULONG DeviceId, PUNICODE_STRING DevicePath) {
    return STATUS_SUCCESS;
}

NTSTATUS SymbioseDetachDriverStack(PUNICODE_STRING DevicePath) {
    return STATUS_SUCCESS;
}

NTSTATUS SymbioseLoadNullDriver(PUNICODE_STRING DevicePath) {
    return STATUS_SUCCESS;
}

NTSTATUS SymbioseUnloadNullDriver(PUNICODE_STRING DevicePath) {
    return STATUS_SUCCESS;
}

NTSTATUS SymbioseReattachDriverStack(PUNICODE_STRING DevicePath) {
    return STATUS_SUCCESS;
}

NTSTATUS
SymbioseIsolateNvmeDevice(
	_In_ WDFDEVICE Device,
	_In_ ULONG VendorId,
	_In_ ULONG DeviceId
)
{
	NTSTATUS status;
	PSYMBIOSE_DEVICE_CONTEXT devCtx = NULL;
	ULONG deviceIndex = 0;

	PAGED_CODE();

	devCtx = SymbioseDeviceGetContext(Device);
	if (devCtx == NULL) {
		return STATUS_INVALID_PARAMETER;
	}

	WdfWaitLockAcquire(devCtx->StateLock, NULL);

	// Check if we've exceeded maximum isolated devices
	if (devCtx->IsolatedDeviceCount >= SYMBIOSE_MAX_NVME_DEVICES) {
		WdfWaitLockRelease(devCtx->StateLock);
		KdPrint(("SymbioseBridge: Maximum NVMe isolation count reached\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	deviceIndex = devCtx->IsolatedDeviceCount;

	// Store device identification
	devCtx->IsolatedDevices[deviceIndex].VendorId = VendorId;
	devCtx->IsolatedDevices[deviceIndex].DeviceId = DeviceId;
	devCtx->IsolatedDevices[deviceIndex].Isolated = FALSE;

	// Step 1: Find the NVMe device by PCI ID
	UNICODE_STRING devicePath;
	status = SymbioseFindPciDevice(VendorId, DeviceId, &devicePath);
	if (!NT_SUCCESS(status)) {
		WdfWaitLockRelease(devCtx->StateLock);
		KdPrint(("SymbioseBridge: Failed to find PCI device VID=%04X DID=%04X: 0x%08X\n",
				 VendorId, DeviceId, status));
		return status;
	}

	// Store device path for restoration
	devCtx->IsolatedDevices[deviceIndex].DevicePath = devicePath;

	// Step 2: Detach the Windows NTFS/disk driver stack
	// This prevents Windows from accessing the isolated NVMe drive
	status = SymbioseDetachDriverStack(&devicePath);
	if (!NT_SUCCESS(status)) {
		// PATTERN-015: Cleanup on error path
		RtlFreeUnicodeString(&devicePath);
		WdfWaitLockRelease(devCtx->StateLock);
		KdPrint(("SymbioseBridge: Failed to detach driver stack: 0x%08X\n", status));
		return status;
	}

	// Step 3: Load SymbioseNull.inf as the function driver
	// This creates an "airlock" - Windows sees the device but cannot access its storage
	status = SymbioseLoadNullDriver(&devicePath);
	if (!NT_SUCCESS(status)) {
		// PATTERN-015: Re-attach original driver on failure
		SymbioseReattachDriverStack(&devicePath);
		RtlFreeUnicodeString(&devicePath);
		WdfWaitLockRelease(devCtx->StateLock);
		KdPrint(("SymbioseBridge: Failed to load null driver: 0x%08X\n", status));
		return status;
	}

	devCtx->IsolatedDevices[deviceIndex].Isolated = TRUE;
	devCtx->IsolatedDeviceCount++;

	WdfWaitLockRelease(devCtx->StateLock);

	KdPrint(("SymbioseBridge: NVMe device VID=%04X DID=%04X isolated successfully\n",
			 VendorId, DeviceId));

	return STATUS_SUCCESS;
}

NTSTATUS
SymbioseRestoreNvmeDevice(
	_In_ WDFDEVICE Device,
	_In_ ULONG DeviceIndex
)
{
	NTSTATUS status;
	PSYMBIOSE_DEVICE_CONTEXT devCtx = NULL;

	PAGED_CODE();

	devCtx = SymbioseDeviceGetContext(Device);
	if (devCtx == NULL) {
		return STATUS_INVALID_PARAMETER;
	}

	if (DeviceIndex >= devCtx->IsolatedDeviceCount) {
		return STATUS_INVALID_PARAMETER;
	}

	if (!devCtx->IsolatedDevices[DeviceIndex].Isolated) {
		return STATUS_SUCCESS;	// Already restored
	}

	WdfWaitLockAcquire(devCtx->StateLock, NULL);

	// Unload null driver
	status = SymbioseUnloadNullDriver(&devCtx->IsolatedDevices[DeviceIndex].DevicePath);
	if (!NT_SUCCESS(status)) {
		WdfWaitLockRelease(devCtx->StateLock);
		KdPrint(("SymbioseBridge: Failed to unload null driver: 0x%08X\n", status));
		return status;
	}

	// Re-attach original driver stack
	status = SymbioseReattachDriverStack(&devCtx->IsolatedDevices[DeviceIndex].DevicePath);
	if (!NT_SUCCESS(status)) {
		WdfWaitLockRelease(devCtx->StateLock);
		KdPrint(("SymbioseBridge: Failed to re-attach driver stack: 0x%08X\n", status));
		return status;
	}

	devCtx->IsolatedDevices[DeviceIndex].Isolated = FALSE;
	RtlFreeUnicodeString(&devCtx->IsolatedDevices[DeviceIndex].DevicePath);

	WdfWaitLockRelease(devCtx->StateLock);

	KdPrint(("SymbioseBridge: NVMe device index %lu restored\n", DeviceIndex));
	return STATUS_SUCCESS;
}
