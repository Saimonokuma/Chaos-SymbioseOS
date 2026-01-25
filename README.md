# Project CoSMOS Resurrection: Architectural Blueprint
### Implementation for Chaos-OS on Atlas OS via AME Wizard Automation

**Author:** USER & NOVA VIOLET
**Date:** January 2026
**Target Platform:** Windows 10 (Atlas OS Build)
**Hardware Target:** AMD Threadripper 9880X / NVIDIA RTX 5090

---

## 1. Introduction: The Symbiotic Paradigm

The quest for high-performance computing (HPC) clusters has historically been bifurcated into two distinct domains: the ubiquity of the Windows desktop environment and the raw, unencumbered power of the Linux kernel. In the early 2000s, Ian Latter and the Midnight Code project attempted to bridge this divide with **Project CoSMOS (Chaos-OS)**, a revolutionary approach to ad-hoc clustering.

Rather than relying on traditional Type-1 hypervisors, which introduce virtualization overhead, or dual-boot configurations, which enforce mutual exclusivity, CoSMOS proposed a **parasitic architecture**. It utilized a driver, `linux.sys`, to bootstrap a specialized Linux kernel (Chaos) alongside the running Windows NT kernel, effectively creating a symbiotic organism where two operating systems shared the same hardware substrate.

This research report outlines a comprehensive modernization of Project CoSMOS, targeting the contemporary **Atlas OS** platform. Atlas OS, a modified version of Windows optimized for performance and latency reduction, provides an ideal host environment due to its minimized background processes and stripped telemetry. The core challenge lies in porting the legacy **WDM (Windows Driver Model)** architecture of `linux.sys` to the modern, secure, and robust **WDF (Windows Driver Framework)**.

This implementation plan details the creation of `symbiose_bridge.sys`, a **Kernel-Mode Driver Framework (KMDF)** driver designed to orchestrate the **"Dark Layer"**—a region of physical memory excised from the host OS to house the guest kernel. It further explores 32-bit to 64-bit thunking to support legacy loaders, the "Null Driver" technique for passing next-generation hardware (specifically the **NVIDIA RTX 5090**) to the guest, and the complete automation of this injection process using **AME Wizard Playbooks (.apbx)**.

### 1.1 Historical Context: The Legacy of Midnight Code
The original CoSMOS architecture relied on the permissiveness of Windows 2000/XP. Ian Latter’s whitepapers on Security and openMosix detailed how clustering could be achieved over untrusted networks using the CHAOS distribution. The pivotal component, `linux.sys`, functioned by allocating a large block of non-paged pool memory, loading the Linux kernel image into it, and then hijacking the CPU instruction pointer to switch contexts.

**Modern Windows architectures (Windows 10/11 x64) introduce significant barriers:**
* **Kernel Patch Protection (PatchGuard):** Prevents modification of the IDT and SSDT.
* **Driver Signature Enforcement (DSE):** Mandates signed kernel-mode code.
* **ASLR:** Complicates memory structure prediction.

The resurrection of CoSMOS on Atlas OS requires navigating these security boundaries not by disabling them entirely, but by utilizing legitimate kernel APIs provided by the WDF to achieve the same "memory theft" and execution transfer results.

---

## 2. The Host Environment: Atlas OS and AME Wizard Automation

The deployment vector for the modern CoSMOS implementation is the **AME Wizard**, a sophisticated configuration tool that applies "Playbooks" to modify the Windows operating system.

### 2.1 AME Wizard Architecture and Playbooks
AME Wizard Playbooks (`.apbx` files) are encrypted 7-Zip archives containing configuration YAMLs, scripts, and binaries. The Wizard utilizes a backend engine known as **TrustedUninstaller**, which is capable of executing actions with **TrustedInstaller privileges**—surpassing standard Administrator rights.

**Automation Capabilities:**
* **Elevate Privileges:** Execute setup scripts as TrustedInstaller.
* **Modify Boot Configuration:** Automate `bcdedit` commands for Test Signing.
* **Inject Driver Binaries:** Securely place drivers in the Driver Store.

### 2.2 Playbook Configuration Structure
To automate the deployment, a custom Playbook must be constructed.

**Table 1: Proposed `playbook.conf` Structure for CoSMOS**

| Element | Value | Description |
| :--- | :--- | :--- |
| `<Name>` | Project CoSMOS | Internal identifier. |
| `<Version>` | 2.0.0 | Versioning for AME Wizard update tracking. |
| `<ShortDescription>` | Chaos-OS Injection | Summary for the UI. |
| `<Requirements>` | DefenderDisabled | Ensuring AV does not flag the memory hiding driver. |
| `<SupportedBuilds>` | 22631 | Targeting Windows 11 23H2 (Atlas OS base). |

---

## 3. Kernel Driver Development: symbiose_bridge.sys (WDF)

The transition from WDM to WDF is critical for stability. WDF provides a managed object model that abstracts complexity.

### 3.1 Driver Entry and Initialization
The `symbiose_bridge.sys` driver initializes by creating a `WDFDEVICE` object and a symbolic link (e.g., `\DosDevices\Symbiose`) to allow user-mode communication. It acts as a root-enumerated device to establish the control channel.

### 3.2 The "Dark Layer": Implementing Memory Hiding
The fundamental requirement is the creation of a "Dark Layer"—a contiguous block of physical RAM dedicated to the Chaos kernel.

#### 3.2.1 Contiguous Allocation Challenges
On high-end systems (RTX 5090 / 384GB RAM), we must avoid the 4GB legacy limit.
* **Modern Approach:** `symbiose_bridge.sys` must set `HighestAcceptableAddress` to `MAXULONG64` (`0xFFFFFFFFFFFFFFFF`) to allocate the Dark Layer in high memory.

#### 3.2.2 The "Vanishing" Mechanism
To fully hide the memory, we prevent Windows from managing these pages.

* **Method A: MDL Locking (Recommended):** The driver allocates an MDL for the buffer and calls `MmProbeAndLockPages`. This prevents paging out.
* **Method B: PFN Database Manipulation (DKOM):** Direct modification of `MmPfnDatabase` to remove pages from the OS working set. *Risk: PatchGuard Trigger.*

**Implementation Logic (Pseudo-code):**

```c
// WDF Context for Memory Allocation
PHYSICAL_ADDRESS max_addr;
max_addr.QuadPart = MAXULONG64; // Allow allocation anywhere in 64-bit space
SIZE_T dark_layer_size = 8ULL * 1024 * 1024 * 1024; // 8 GB reserved for Kernel

// 1. Allocate Contiguous Physical Memory
PVOID virtual_address = MmAllocateContiguousMemory(dark_layer_size, max_addr);
if (!virtual_address) return STATUS_INSUFFICIENT_RESOURCES;

// 2. Lock the memory (MDL) to prevent paging
PMDL mdl = IoAllocateMdl(virtual_address, dark_layer_size, FALSE, FALSE, NULL);
MmBuildMdlForNonPagedPool(mdl);
// This effectively creates the "Dark Layer" by pinning it in RAM
MmProbeAndLockPages(mdl, KernelMode, IoWriteAccess); 

// 3. Retrieve Physical Address for the Chaos Loader
PHYSICAL_ADDRESS chaos_pa = MmGetPhysicalAddress(virtual_address);
```

### 3.3 32-bit to 64-bit Thunking
Since legacy Chaos tools are 32-bit, `symbiose_bridge.sys` must detect and translate requests using `WdfRequestIsFrom32BitProcess`.

**Code Snippet (Thunking Logic):**

```c
// Handling IOCTL_LOAD_CHAOS
if (WdfRequestIsFrom32BitProcess(Request)) {
    // Defines input buffer as 32-bit struct
    PCHAOS_BOOT_PARAMS_32 params32;
    status = WdfRequestRetrieveInputBuffer(Request, sizeof(CHAOS_BOOT_PARAMS_32), &params32, NULL);
    
    // Thunk to 64-bit internal format
    internal_params.KernelBase = (UINT64)params32->KernelBase;
    internal_params.RamdiskSize = params32->RamdiskSize;
} else {
    // Native 64-bit handling
    PCHAOS_BOOT_PARAMS params64;
    status = WdfRequestRetrieveInputBuffer(Request, sizeof(CHAOS_BOOT_PARAMS), &params64, NULL);
    internal_params = *params64;
}
```

---

## 4. Hardware Passthrough: The RTX 5090 Integration

To allow Chaos to control the RTX 5090, we must "unbind" the Windows driver (`nvlddmkm.sys`).

### 4.2 The "Null Driver" Strategy
We install a "Null Driver" for the specific GPU instance. This leaves the device in a dormant state but visible on the PCI bus.

**Snippet of `SymbioseNull.inf`:**

```ini
[Version]
Signature="$WINDOWS NT$"
Class=System
Provider=%MidnightCode%
DriverVer=01/01/2026,1.0.0.0

[Manufacturer]
%MidnightCode%=Symbiose,NTamd64

; Specific Hardware ID for RTX 5090 (Example ID)
"NVIDIA RTX 5090 (Chaos Mode)" = NullDriverInstall, PCI\VEN_10DE&DEV_XXXX

; Critical: The ',2' flag specifies a Null Service
AddService = ,2
```

### 4.3 Kernel-Level Retrieval
Once the Null Driver is applied, `symbiose_bridge.sys` scans the PCI bus (`HalGetBusData`), retrieves the BAR addresses, and passes them to the Chaos Kernel for `ioremap`.

---

## 5. Automated Deployment Plan: The AME Wizard Playbook

The `.apbx` file automates the setup.

### 5.1 Playbook Directory Structure

```text
Playbook Root
├── playbook.conf
├── Configuration
│   ├── main.yml
│   └── Tasks
│       ├── 01_system_prep.yml
│       ├── 02_driver_inject.yml
│       └── 03_gpu_isolation.yml
└── Executables
    ├── symbiose_bridge.sys
    ├── symbiose_bridge.inf
    ├── SymbioseNull.inf
    └── Tools
        ├── devcon.exe
        └── ChaosLoader.exe
```

### 5.2 YAML Logic for Injection

**Step 1: System Preparation**
```yaml
# Configuration/Tasks/01_system_prep.yml
actions:
  - !writeStatus: {status: 'Configuring Boot Environment'}
  - !cmd:
      command: 'bcdedit /set testsigning on'
      runAs: TrustedInstaller
  - !cmd:
      command: 'bcdedit /set nointegritychecks on'
      runAs: TrustedInstaller
```

**Step 2: Driver Injection**
```yaml
# Configuration/Tasks/02_driver_inject.yml
actions:
  - !writeStatus: {status: 'Injecting Symbiose Kernel Bridge'}
  # Register the Service manually in Registry
  - !registryKey: {key: 'HKLM\SYSTEM\CurrentControlSet\Services\SymbioseBridge'}
  - !registryValue:
      key: 'HKLM\SYSTEM\CurrentControlSet\Services\SymbioseBridge'
      value: 'ImagePath'
      data: '\SystemRoot\System32\Drivers\symbiose_bridge.sys'
      type: REG_EXPAND_SZ
```

**Step 3: GPU Isolation**
```yaml
# Configuration/Tasks/03_gpu_isolation.yml
actions:
  - !writeStatus: {status: 'Isolating RTX 5090 Hardware'}
  # Force update the specific device ID to the Null Driver
  - !run:
      exe: '%PLAYBOOK%\Executables\Tools\devcon.exe'
      args: 'update "%PLAYBOOK%\Executables\SymbioseNull.inf" "PCI\VEN_10DE&DEV_XXXX"'
      runAs: TrustedInstaller
```

---

## 6. Conclusion & Comparison

Project CoSMOS on Atlas OS represents the convergence of high-performance Windows optimization and parasitic computing. By replacing the legacy WDM architecture with a WDF `symbiose_bridge.sys` and automating deployment via AME Wizard, we create a reproducible reality for the modern power user.

**Table 2: Comparison of Memory Hiding Techniques**

| Feature | MmRemovePhysicalMemory (Legacy) | PFN Database DKOM (Rootkit) | MmAllocate + MDL Lock (WDF) |
| :--- | :--- | :--- | :--- |
| **Mechanism** | Hot-unplug API (Hyper-V) | Direct modification of `MmPfnDatabase` | Standard Kernel Allocation & Locking |
| **Stealth** | Moderate | High (Invisible to OS) | Low (Visible as "Driver Locked" RAM) |
| **Stability** | Low (BSOD risk) | Low (PatchGuard Trigger Risk) | **High (Native WDF Support)** |
| **Atlas OS Suitability** | Not Recommended | Experimental Only | **Recommended** |
