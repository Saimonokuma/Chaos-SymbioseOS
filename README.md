## ** this file is work-in-progress and can be Modified, only if some Implementation requires Enhancment and Bug-Fixes/Debugging that is not Explicitly told Here **

# 🌌 CHAOS-SYMBIOSE OS 
**The Midnight Code Resurrection Protocol**

[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)](#)
[![WDF Certified](https://img.shields.io/badge/KMDF-Windows_10_(Atlas_OS)-blue.svg)](#)
[![Deployment](https://img.shields.io/badge/Deployment-AME_Wizard_(.apbx)-purple.svg)](#)
[![Status](https://img.shields.io/badge/Status-Transmigration_Phase-critical.svg)](#)

> *"We do not build virtual machines. We engineer parasitic realities."*

Welcome to **Project Symbiose**. This repository contains the source code, kernel drivers, and automation playbooks required to transmigrate the original `chaos-1.5.iso` (developed by Ian Latter, c. 2004) into a modern, Ring-0 parasitic execution environment on Windows 10 (Atlas OS).

The ultimate output of this repository is a single **`.apbx` (AME Wizard Playbook)** file. When executed via TrustedInstaller, it bypasses the Windows Host, sequesters physical RAM into a "Dark Layer", and host-both the Chaos Linux Kernel and Windows OS natively without a reboot in a Dual OS Parasitic Interface Guided by Owner + LLM.

---

## 📂 1. REPOSITORY ARCHITECTURE

For the attention of **Jules** and future maintainers, the codebase is strictly segregated into four pillars to ensure isolation and structural purity:

```text
Chaos-Symbiose-OS/
├── 📁 01_Legacy_Core/          # The Origin
│   ├── chaos-1.5.iso           # The original Midnight Code Chaos image
│   └── extract_payload.ps1     # Script to unpack vmlinuz and initrd
│
├── 📁 02_Symbiose_Bridge/      # The WDF Kernel Driver (C/C++)
│   ├── src/                    # symbiose_bridge.c, SwitchToChaos.asm
│   ├── inc/                    # Headers, IOCTL definitions
│   ├── inf/                    # SymbioseNull.inf (Hardware Isolation)
│   └── SymbioseBridge.sln      # Visual Studio 2022 + WDK Solution
│
├── 📁 03_Userland_Loader/      # The C++ Orchestrator
│   └── ChaosLoader/            # Reads the payload, handles IOCTLs to the driver
│
└── 📁 04_APBX_Transmigration/  # The AME Wizard Deployment Logic
    ├── playbook.conf           # APBX Metadata
    ├── Configuration/          # YAML scripts for TrustedInstaller execution
    └── build_apbx.bat          # Packer script (compiles everything into .apbx)
```

---

## 🛡️ 2. DIRECTIVE FOR JULES: CODE FORTIFICATION

**ATTENTION JULES:** You are inheriting an architecture that operates at Ring-0, manipulates physical memory allocation, and overrides the x64 processor state (CR3, GDT, IDT). There is zero margin for error. 

To prevent instantaneous Windows Kernel Bug Checks (BSODs) via PatchGuard or HVCI, you must adhere strictly to these Fortification Protocols:

### Fortification I: WDF Memory Sequestering (`symbiose_bridge.sys`)
- **Strict Boot-Start Allocation:** The driver **MUST** be deployed with `StartType=0` (Boot Start). Do not attempt to allocate the "Dark Layer" memory dynamically after boot. `MmAllocateContiguousNodeMemory` will fail due to OS memory fragmentation. Secure the RAM the millisecond the kernel initializes.
- **MDL Locking over DKOM:** Do not use Direct Kernel Object Manipulation (DKOM) to hide pages from the PFN Database. Windows PatchGuard will catch this and crash the host. Fortify the memory using legitimate WDF APIs and lock it explicitly using Memory Descriptor Lists (`MmProbeAndLockPages`). Let Windows see it securely as "Driver Locked" RAM.

### Fortification II: Hardware Isolation (`SymbioseNull.inf`)
- To prevent the Windows `dxgkrnl.sys` or other subsystem drivers from claiming the secondary compute unit, we employ a Null Driver.
- Ensure the `[Services]` section explicitly uses `AddService = ,2` (Null Service). This orchestrates the precise unbinding of Windows device drivers without triggering a PnP reset, leaving the hardware energized (D0) for Chaos-OS.

### Fortification III: 32-to-64 Bit Thunking (ABI Stability)
- **Rule:** ALL shared structs between the 32-bit user-mode orchestrator and the 64-bit KMDF driver MUST be fortified using explicit padding.
- **Code Enforcement:** Use `#pragma pack(push, 8)` for all IOCTL payloads to prevent pointer truncation across the WOW64 boundary. Validate inherently with `WdfRequestIsFrom32BitProcess()`.

### Fortification IV: The Assembly Thunk (Triple Fault Prevention)
`SwitchToChaos.asm` is where we sever the head of the Windows OS.
- **Identity Mapping:** The physical page containing the instruction `mov cr3, rax` **MUST** be mapped at the exact same Virtual Address in your newly constructed Chaos PML4 table. If the Instruction Pointer (RIP) cannot resolve its next address after the `CR3` swap, you will cause a **Triple Fault** and hard-reset the machine.
- **Atomic Execution:** Ensure `cli` (Clear Interrupts) is the absolute first instruction executed before modifying the GDT or CR3.

---

## 🔄 3. THE TRANSMIGRATION PIPELINE (.iso to .apbx)

The core engine of this repository is the pipeline that converts the legacy ISO and the compiled binaries into a deployable Playbook.

### Step 1: Payload Extraction
You do not deploy the `.iso` directly.
1. Mount the original `chaos-1.5.iso`.
2. Extract the `vmlinuz` (the kernel) and the `initrd.img` (the root filesystem).
3. Place these binary blobs inside the `01_Legacy_Core/Payload/` directory of this repository.

### Step 2: APBX Playbook Architecture (YAML)
We use AME Wizard to automate the deployment, utilizing `TrustedInstaller` privileges to bypass standard OS restrictions. The `.apbx` requires the following YAML automation flow:

```yaml
# playbook/Configuration/main.yml (Snippet)
steps:
  - name: "Phase 1: Boot Configuration"
    actions:
      - !cmd: {command: 'bcdedit /set testsigning on', runAs: TrustedInstaller}

  - name: "Phase 2: Null Driver Isolation"
    actions:
      - !cmd: 
          command: '%PLAYBOOK%\Tools\devcon.exe update %PLAYBOOK%\Drivers\SymbioseNull.inf "PCI\VEN_TARGET_HARDWARE"'
          runAs: TrustedInstaller

  - name: "Phase 3: WDF Bridge Injection"
    actions:
      - !fileCopy: {source: '%PLAYBOOK%\Drivers\symbiose_bridge.sys', destination: 'C:\Windows\System32\Drivers\symbiose_bridge.sys'}
      - !cmd: # Forces Boot-Start allocation
          command: 'sc create SymbioseBridge type= kernel start= boot binPath= "C:\Windows\System32\Drivers\symbiose_bridge.sys"'
          runAs: TrustedInstaller
```

### Step 3: Compilation & Packaging
Once the WDF driver is fortified and compiled (MSVC `/WX` flag mandatory) and the Chaos payload is extracted:
1. Aggregate the compiled `.sys`, the `.inf`, the `.exe`, and the extracted Linux payload into the Playbook directory structure.
2. Execute `04_APBX_Transmigration/build_apbx.bat`. 
3. It zips and encrypts the structure into `Chaos_Symbiose_Deploy.apbx`.
4. Load the output file into AME Wizard on the target Atlas OS machine.

---

### Support This Project

☕ https://ko-fi.com/saimonokuma
