/*++
 * symbiose_bridge.h — Internal driver prototypes and device context
 *
 * BRIDGE-002: Device context structure
 * BRIDGE-013: Shared internal header
 *
 * Reference: Interactive_Plan.md §III·2 (lines 847-933), §III·3 (lines 942-951)
 *
 * This header is INTERNAL to the KMDF driver (02_Symbiose_Bridge/).
 * For the shared IOCTL interface used by both Ring-0 and Ring-3,
 * see symbiose_ioctl.h in the inc/ directory.
 *
 * Constitutional constraints:
 *   X·1 — NO #include <WinHvPlatform.h>
 *   X·2 — PPO before WdfDeviceCreate
 *   X·3 — Spinlock release before WdfRequestComplete
 *--*/

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <wdm.h>

//
// ── Device Interface GUID ────────────────────────────────────────────────────
// ChaosLoader.exe uses this GUID to open the device via SetupDiGetClassDevs
// Reference: §III·2 Step 4 (line 875-877)
//
// {DEADBEEF-1234-5678-ABCD-EF0123456789}
//
DEFINE_GUID(GUID_DEVINTERFACE_SYMBIOSE_BRIDGE,
    0xDEADBEEF, 0x1234, 0x5678,
    0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89);

//
// ── Maximum BAR regions and interrupt vectors ────────────────────────────────
//
#define SYMBIOSE_MAX_BARS           6
#define SYMBIOSE_MAX_INTERRUPTS     16

//
// ── Device context structure ─────────────────────────────────────────────────
// Stored in the WDF device object's context area.
// Accessed via GetSymbioseDeviceContext(Device).
//
// Reference: §III·3 (lines 944-950)
//
typedef struct _SYMBIOSE_DEVICE_CONTEXT {

    //
    // BAR MMIO mappings — populated by EvtDevicePrepareHardware (BRIDGE-003)
    //
    PVOID       BarVa[SYMBIOSE_MAX_BARS];       // Kernel virtual addresses
    ULONG       BarLen[SYMBIOSE_MAX_BARS];       // Length of each BAR region
    ULONG       BarCount;                        // Number of mapped BARs

    //
    // MSI-X interrupt objects — populated by BRIDGE-004
    //
    WDFINTERRUPT  Interrupt[SYMBIOSE_MAX_INTERRUPTS];
    ULONG         InterruptCount;

    //
    // IOCTL queue — created in SymbioseCreateIoctlQueue (BRIDGE-005)
    //
    WDFQUEUE    IoctlQueue;

    //
    // Inverted-call pending request slots (§III·3)
    // Each async IOCTL channel has its own slot.
    // Constraint X·17: ChaosLoader must keep 3 pending IOCTLs at all times.
    //
    WDFREQUEST volatile PendingVmExitRequest;    // IOCTL_SYMBIOSE_WAIT_VMEXIT
    WDFREQUEST volatile PendingSerialRequest;    // IOCTL_SYMBIOSE_SERIAL_READ
    WDFREQUEST volatile PendingShutdownRequest;  // IOCTL_SYMBIOSE_SHUTDOWN_ACK

    //
    // Device synchronization lock
    // Constraint X·3: MUST be released BEFORE WdfRequestComplete
    //
    WDFSPINLOCK Lock;

    //
    // VMX state — populated by BRIDGE-006
    //
    PVOID               VmxonRegionVa;           // 4KB VMXON region VA (non-paged)
    PHYSICAL_ADDRESS    VmxonRegionPa;           // Physical address for __vmx_on
    PVOID               VmcsVa;                  // 4KB VMCS region VA
    PHYSICAL_ADDRESS    VmcsPa;                  // Physical address for __vmx_vmptrld
    BOOLEAN             VmxEnabled;              // TRUE after successful VMXON

    //
    // EPT state — built by SymbioseEptBuild (BRIDGE-006)
    //
    UINT64      EptPointer;                      // EPTP for VMCS field 0x201A

    //
    // Guest memory — registered via IOCTL_SYMBIOSE_REGISTER_RAM
    //
    PVOID       GuestRamBase;                    // Kernel VA of guest physical RAM
    UINT64      GuestRamPa;                      // Physical address of guest RAM
    UINT64      GuestRamSize;                    // Size in bytes

    //
    // Initrd — loaded via IOCTL_SYMBIOSE_LOAD_INITRD
    //
    UINT64      InitrdPa;                        // Guest physical address of initrd
    UINT64      InitrdSize;                      // Initrd size in bytes

    //
    // Host state for VMX host context
    //
    UINT64      HostCr3;                         // Captured at DeviceAdd time
    PVOID       HostStack;                       // Non-paged stack for VM-Exit
    KEVENT      ShutdownAckEvent;                // Death Rattle sync (BRIDGE-007)

} SYMBIOSE_DEVICE_CONTEXT, *PSYMBIOSE_DEVICE_CONTEXT;

//
// WDF context accessor macro
// Usage: PSYMBIOSE_DEVICE_CONTEXT ctx = GetSymbioseDeviceContext(Device);
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SYMBIOSE_DEVICE_CONTEXT, GetSymbioseDeviceContext)

//
// ── Forward declarations for PnP power callbacks ─────────────────────────────
//
EVT_WDF_DEVICE_PREPARE_HARDWARE     EvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE     EvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY             EvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT              EvtDeviceD0Exit;

//
// ── Forward declarations for IOCTL handler ───────────────────────────────────
//
NTSTATUS SymbioseCreateIoctlQueue(_In_ WDFDEVICE Device);

//
// ── Forward declarations for interrupt handler ───────────────────────────────
//
VOID SymbioseCreateInterrupt(
    _In_ WDFDEVICE Device,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc
    );

//
// ── Forward declarations for VMX operations ──────────────────────────────────
//
NTSTATUS SymbioseVmxOn(VOID);
NTSTATUS SymbioseVmLaunch(VOID);
VOID VmxSetDeviceContext(_In_ PSYMBIOSE_DEVICE_CONTEXT DevCtx);

//
// ── Forward declarations for ACPI / Death Rattle handler ─────────────────────
//
VOID AcpiSetDeviceContext(_In_ PSYMBIOSE_DEVICE_CONTEXT DevCtx);
VOID SymbioseHandleShutdownAck(VOID);
