// 02_Symbiose_Bridge/src/acpi_handler.c
// Crucible: PATTERN-008 (TOCTOU prevention), PATTERN-015 (cleanup on error)

#include "symbiose_bridge.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, SymbioseAcpiNotificationCallback)
#endif

VOID
SymbioseAcpiNotificationCallback(
	_In_ PVOID Context,
	_In_ ULONG NotifyCode
)
{
	PSYMBIOSE_DEVICE_CONTEXT devCtx = NULL;
	WDFDEVICE device = (WDFDEVICE)Context;

	PAGED_CODE();

	if (device == NULL) {
		KdPrint(("SymbioseBridge: ACPI callback with NULL device\n"));
		return;
	}

	devCtx = SymbioseDeviceGetContext(device);
	if (devCtx == NULL) {
		KdPrint(("SymbioseBridge: ACPI callback with NULL context\n"));
		return;
	}

	switch (NotifyCode) {
	case SYMBIOSE_ACPI_NOTIFY_SHUTDOWN:
		KdPrint(("SymbioseBridge: ACPI shutdown notification received\n"));

		// Acquire state lock to prevent concurrent state changes
		WdfWaitLockAcquire(devCtx->StateLock, NULL);

		// PATTERN-008: Atomic state transition
		if (devCtx->State != SymbioseStateChaosRunning) {
			KdPrint(("SymbioseBridge: Shutdown received but Chaos not running (state=%d)\n",
					 devCtx->State));
			WdfWaitLockRelease(devCtx->StateLock);
			// Allow Windows to proceed with normal shutdown
			return;
		}

		devCtx->State = SymbioseStateShutdownPending;
		WdfWaitLockRelease(devCtx->StateLock);

		// Signal the LLM that shutdown is imminent
		KeSetEvent(&devCtx->ShutdownEvent, IO_NO_INCREMENT, FALSE);

		// Start ACK timeout timer
		LARGE_INTEGER timeout;
		timeout.QuadPart = WDF_REL_TIMEOUT_IN_MS(SYMBIOSE_ACK_TIMEOUT_MS);
		KeSetTimer(&devCtx->AckTimer, timeout, NULL);

		KdPrint(("SymbioseBridge: Waiting for LLM ACK (timeout=%d ms)\n",
				 SYMBIOSE_ACK_TIMEOUT_MS));
		break;

	case SYMBIOSE_ACPI_NOTIFY_SUSPEND:
		KdPrint(("SymbioseBridge: ACPI suspend notification received\n"));
		// Suspend handling - future implementation
		break;

	case SYMBIOSE_ACPI_NOTIFY_RESUME:
		KdPrint(("SymbioseBridge: ACPI resume notification received\n"));
		// Resume handling - future implementation
		break;

	default:
		KdPrint(("SymbioseBridge: Unknown ACPI notification 0x%08X\n", NotifyCode));
		break;
	}
}