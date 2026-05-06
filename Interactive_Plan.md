# 🌌 CHAOS-SYMBIOSE OS — Autonomous Agent Workflow Blueprint V3

**Project ID:** `CHAOS-SYMB-2026-V3`
**Classification:** `ELITE_SYSTEMS_ARCH`
**Status:** `AGENT_READY`
**Target:** `Windows 10/11 (NT-Core) → Chaos 1.5 (Linux-Custom) Hybrid`

---

## I. REPOSITORY ARCHITECTURE

### I·1 Current Seed State
The `main` branch contains the extracted Chaos 1.5 archive inside `CHAOS 1.5/`. The original ISO has already been unpacked. Do not parse the legacy filesystem dynamically. 90% of the 2004 tree is dead tissue.

**Transplant only:**
- `BZIMAGE` — the raw Linux kernel
- `CHAOS.RDZ` — the initial ramdisk (contains legacy OpenMosix source for NX upgrade)

### I·2 Target Blueprint

```
Chaos-Symbiose-OS/
├── 📁 CHAOS 1.5/                    # SEED: Untouched archive (BZIMAGE + CHAOS.RDZ)
├── 📁 02_Symbiose_Bridge/           # BUILD: WDF Kernel Driver (symbiose_bridge.sys)
│   ├── src/
│   │   ├── symbiose_bridge.c         # ACPI hook, hardware handoff, Ring-0 VMX root
│   │   ├── ioctl_handler.c           # Inverted call IPC
│   │   ├── vmx_hypervisor.c          # Native KMDF VT-x/AMD-V hypervisor implementation
│   │   └── SwitchToChaos.asm         # Assembly thunk (VMXON/VMLAUNCH, identity mapping)
│   └── inf/
│       └── SymbioseNull.inf          # GPU & NVMe hardware isolation driver
├── 📁 03_HiveMind_Orchestrator/     # BUILD: MoE, Context Paging, Clustering
│   ├── ChaosLoader/                  # Userspace orchestrator sending BZIMAGE to KMDF
│   ├── IRCd_Neural_Bus/             # Custom IRCv3 daemon for LLM Scout M2M
│   ├── VFS_Storage_Manager/          # Vectorized NVMe direct-access (Hippocampus)
│   └── openmosix_nx/                 # Modernized OpenMosix + eBPF/CRIU
├── 📁 04_APBX_Transmigration/       # BUILD: AME Wizard deployment package (.apbx)
│   └── playbook/
│       ├── playbook.conf             # Security constraints & target OS schema
│       ├── Configuration/
│       │   ├── main.yml              # Master orchestration script
│       │   └── Tasks/
│       │       ├── phase0_config.yml
│       │       ├── telemetry_bind.yml
│       │       ├── vbs_annihilate.yml
│       │       └── hardware_airlock.yml
│       └── Executables/
│           ├── ChaosLoader.exe
│           ├── symbiose_ircd.exe
│           ├── bzImage               # Transplanted from CHAOS 1.5/
│           ├── initrd.img            # Rebuilt ramdisk (CRIU + eBPF + hive_mind init)
│           └── Drivers/
│               └── SymbioseNull.inf
├── 📁 05_Integration_Tests/         # BUILD: QEMU + Smoke tests
├── 📁 .github/
│   └── workflows/
│       └── forge-apbx.yml            # CI/CD pipeline
├── 📜 toolchain-x86_64-w64-mingw32.cmake
└── 📜 README.md
```

---

## II. CI/CD FORGE & PIPELINE ARCHITECTURE

The `forge-apbx.yml` workflow cross-compiles all Windows executables on an `ubuntu-24.04` runner using `mingw-w64`.
- **Mingw-w64** is used for reduced compilation time, aggressive `-O3 -flto` flags, and reproducible sterile builds.
- **WDK Build Step:** Compiles `symbiose_bridge.sys` using a sterile environment.
- **APBX Sealing:** The final step seals the `.apbx` artifact using `7z -t7z -p"malte" -mhe=on -m0=lzma2 -mx=9`. The password `malte` is mandatory for AME Wizard compatibility.

---

## III. THE KMDF BRIDGE ENGINE & NATIVE HYPERVISOR

**CRITICAL PARADIGM SHIFT:** The architecture explicitly **rejects** user-mode virtualization APIs (like WHPX) and user-mode driver frameworks (UMDF2). Instead, the custom KMDF driver (`symbiose_bridge.sys`) natively drops the CPU into VMX root operation (Ring -1) and acts as a bare-metal hypervisor.

### III·1 Core Framework: Absolute KMDF Dominance
- **Direct Memory Access (DMA):** Mandatory for hardware passthrough; KMDF supports memory mapping physical addresses and managing Extended Page Tables (EPT).
- **Native VMX/SVM Control:** By operating in Ring 0, the driver can execute `VMXON`, configure the Virtual Machine Control Structure (VMCS), and launch the guest via `VMLAUNCH`, entirely bypassing Microsoft's virtualization stack.
- **Arbitrary Buffer Access:** KMDF supports the `METHOD_NEITHER` I/O buffering model for zero-copy IPC.

### III·2 Advanced Power Management & ACPI Interception
The Symbiose Bridge driver must serve as the Power Policy Owner (PPO). 
- `WdfDeviceInitSetPowerPolicyOwnership(TRUE)` MUST be called **before** `WdfDeviceCreate`.
- **D3cold Capabilities:** The driver uses `WdfDeviceAssignS0IdleSettings` to define idle capabilities (`IdleCanWakeFromS0`).
- **PnP Notification:** When intercepting `GUID_DEVICE_INTERFACE_ARRIVAL`, the handle creation MUST be offloaded to asynchronous KMDF Work Items to prevent deadlocking the PnP manager.

### III·3 MSI-X Implementation and Interrupt Routing
INTx is explicitly deprecated. The Bridge exclusively utilizes Message-Signaled Interrupts eXtended (MSI-X).
- **INF Configuration:** The `MessageSignaledInterruptProperties` subkey must have `MSISupported` set to 1.
- **Execution Levels:** If the parent object executes at `WdfExecutionLevelPassive`, the driver may securely supply an `EvtInterruptWorkItem` but strictly *cannot* supply an `EvtInterruptDpc` or it will bugcheck.

---

## IV. THE IPC CONTROL PLANE & SHARED MEMORY

### IV·1 Inverted Call Paradigm
Traditional IOCTLs block the thread. Instead, the `ChaosLoader` host preemptively dispatches async IOCTL requests to the KMDF driver, which are placed in a pending WDFQUEUE. Upon hardware interrupt or VM-Exit from the guest, the ISR/DPC completes the IRP. **All spinlocks must be explicitly released prior to request completion** to prevent hard kernel deadlocks.

### IV·2 Correlated Messaging (IRCv3 Batch protocols)
Using IRCv3 batch protocol concepts, the host assigns a unique tag to initial requests. The KMDF driver embeds the label in all async responses related to that transaction, utilizing batch start/end markers to logically group fragmented memory addresses.

### IV·3 Zero-Copy Shared Memory
- `MmMapIoSpace` translates physical bus addresses to kernel virtual addresses.
- `WdfMemoryCreate` paired with `MmMapLockedPagesSpecifyCache` directly maps physical pages into the user-mode virtual address space.
- The KMDF driver registers this window directly into the Extended Page Tables (EPT) of the guest, enabling zero-copy, VM-Exit-free memory sharing.

### IV·4 GPU Observability via Userspace eBPF
The architecture utilizes `bpftime` to inject userspace eBPF directly into the proprietary GPU driver stack (`libcudart.so`). It intercepts `cuMemAlloc`, `cudaMemcpy`, and `cuLaunchKernel` to provide nanosecond-accurate telemetry into shared eBPF maps.

---

## V. NATIVE KMDF VIRTUALIZATION & BOOT PROTOCOL

### V·1 VMX Root Partition Initialization
- The KMDF driver allocates contiguous, non-paged memory for the VMCS region via `MmAllocateContiguousMemory`.
- EPTs (Extended Page Tables) are constructed manually within the driver to establish the Guest Physical Address (GPA) space, enabling exact control over the 4096-byte page execution flags.

### V·2 Linux Boot Protocol 2.13+ (Via KMDF)
- The user-mode `ChaosLoader` parses the JSON config, loads the `BZIMAGE` and `initrd`, and sends them to the KMDF driver via massive `METHOD_NEITHER` IOCTLs.
- The KMDF driver constructs the "zero page" (`struct boot_params`).
- Sets `cmd_line_ptr` (0x0228) and `ramdisk_image` (0x0218) in the `setup_header` (offset 0x01f1).
- Sets `xloadflags` bit 1 (`XLF_CAN_BE_LOADED_ABOVE_4G`) to allow boot params above the 4GB boundary.
- The KMDF driver configures the VMCS guest state with `RIP=0x100000` and `RSI` pointing to the zero page GPA, then executes `VMLAUNCH`.

### V·3 VM-Exit Interception & Triple Fault Recovery
If the kernel panics, the VMX processor triggers a VM-Exit with the appropriate exit reason. The KMDF driver catches this, dumps `CR0`, `CR2`, `CR3`, and `RIP` from the VMCS, and passes the crash dump back to `ChaosLoader` via the pending inverted-call IRPs for diagnostic display.

---

## VI. DEPLOYMENT ORCHESTRATION: DDA & SECURITY

### VI·1 Discrete Device Assignment (DDA)
- `Disable-PnpDevice` → `Dismount-VMHostAssignableDevice -Force` → `Add-VMAssignableDevice`
- **MMIO Anomaly:** Enterprise GPUs require massive High MMIO space. The host must auto-calculate:
  `Set-VM -HighMemoryMappedIoSpace (2 × BAR1_Size × GPU_Count)`
  Failing this results in a Code 12 "Insufficient Resources" error.

### VI·2 Win11 24H2 Security Hardening (VBS/HVCI)
VBS, HVCI, and Memory Integrity **will completely block** our custom KMDF VMX hypervisor from executing `VMXON`.
- Persistent Group Policy overrides must be configured in `HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard` to permanently disable VBS/HVCI.
- Protected modifications require TrustedInstaller execution, achieved via `IRegisteredTask::RunEx` passing `NT SERVICE\TrustedInstaller`.

---

## VII. IRC HIVE MIND PROTOCOL

### VII·1 Channel Topology
The `symbiose_ircd.exe` is a Ring 3 custom IRCv3 daemon operating entirely over Shared Memory.
- `#oracle`: Central LLM orchestration.
- `#recon`: Scout intelligence.
- `#hive-mind`: Inter-agent coordination.
- `#cluster-announce`: Node discovery.
- `#telemetry`: Real-time metrics.

### VII·2 Jumbo Payloads
IRC 512-byte limit is bypassed using 512MB shared memory (`SymbioseIRCd_PayloadBuffer`). The daemon writes the payload to SHM and sends a TAGMSG with the `payload_id` and `checksum` (CRC64) over IRC.

### VII·3 Death Rattle Protocol
On ACPI shutdown, the KMDF driver sends `SHUTDOWN_IMMINENT` to `#oracle` via the IPC queue. It blocks the power IRP for 30 seconds until the LLM replies with `ACK_READY_TO_DIE`, ensuring state persistence.

---

## VIII. OPENMOSIX CLUSTERING ENGINE

The legacy Chaos 1.5 OpenMosix patch is modernized for **Heterogeneous Tensor Migration**.
- **CRIUgpu:** When a node throttles, CRIU freezes the process, eBPF serializes VRAM via `cudaMemcpy`, streams over RDMA (`libibverbs`), and CRIU restores on target node.
- **Node Discovery:** Replaces UDP `omdiscd` with `#cluster-announce` IRC TAGMSG payloads.
- **Load Balancing:** Scored by GPU thermal, VRAM capacity, and inference queue depth.

---

## IX. AME WIZARD DEPLOYMENT & PHASE 0 CONFIG

### IX·1 Phase 0 User Configuration (Interactive UI)
AME Wizard interactive screen generates `symbiose_config.json`:
1. **GPU/NVMe Selection:** Choose hardware to surrender to Symbiose. NVMe flagged as Continuous Context Drive (CCD).
2. **RAM/vCPU Allocation:** Select vCPUs. Pinned NUMA configurations.
3. **Execution Mode:** Ramdisk vs Disk-backed.
4. **LLM Model:** Pick GGUF/safetensors.
5. **MMIO Auto-Calc:** Applies MMIO formula for DDA.

### IX·2 APBX Structure
- `playbook.conf` dictates LZMA2 encrypted headers, `malte` password.
- `main.yml` orchestrates 4 phases: Boot config (VBS annihilation), Hardware Airlock, IPC Bus deployment, Hypervisor Bootstrap.

---

## X. CRITICAL CONSTRAINTS FOR CODE GENERATION

1. **NO WHPX:** Do not utilize Microsoft's `WinHvPlatform.h` user-mode APIs. The KMDF driver serves as the hypervisor.
2. `WdfDeviceInitSetPowerPolicyOwnership(TRUE)` **before** `WdfDeviceCreate` — bugcheck on violation.
3. Release all spinlocks **before** `WdfRequestComplete` — deadlock on violation.
4. PnP ARRIVAL handle open must use `WdfWorkItem` — PnP manager deadlock on violation.
5. Execution level contract: PASSIVE parent → use WorkItem not DPC — bugcheck on violation.
6. MMIO formula: `2 × BAR1 × GPU_count` — Code 12 on undersizing.
7. VBS disable needs Group Policy keys for reboot persistence — run-once insufficient on Win11 24H2.
8. `MSISupported=1` in INF — silent INTx fallback on omission.

---

## XI. FULL AGENT TASK MATRIX (CODING SECTION)

This matrix defines the strict assignments and acceptance criteria for all coding tasks across the repository.

### MODULE: CONFIG — Phase 0 User Configuration (AME Wizard Interactive UI)

| ID | Task | Acceptance Criteria | Priority |
|----|------|---------------------|----------|
| CONFIG-001 | **GPU Selection** — Enumerate all available GPUs on the host via `Get-PnpDevice` / WMI; display list with model name, VRAM, PCI location path; user selects which GPU(s) to surrender to Symbiose OS. Warn users with 1 GPU that display output will drop. | User sees GPU list; selected GPU location paths are stored as playbook variables for DDA phases | P0 |
| CONFIG-002 | **NVMe / Storage Selection** — Enumerate NVMe and SATA drives; display model, size, current mount points; user selects which drive(s) become the CCD (Continuous Context Drive / Hippocampus). Warn if drive contains Windows partitions. | Selected drive PCI paths stored for SymbioseNull isolation; drives flagged as CCD for VFS init | P0 |
| CONFIG-003 | **RAM Allocation** — Display total system RAM; slider or input field for how much RAM (in GB) to sequester. | RAM allocation value injected into ChaosLoader `--ram` argument | P0 |
| CONFIG-004 | **CPU / vCPU Allocation** — Display total logical processors; user selects how many vCPUs to assign. | vCPU count injected into ChaosLoader args | P0 |
| CONFIG-005 | **NUMA Configuration** — Show topology diagram. User can enable NUMA-aware allocation which pins vCPUs and RAM to the same node as the surrendered GPU. | NUMA-pinned allocation produces measurably lower latency on multi-socket systems | P1 |
| CONFIG-006 | **Execution Mode Selection** — User selects Ramdisk Mode (volatile) or Disk-backed Mode (persistent rootfs). | Mode flag injected into ChaosLoader | P0 |
| CONFIG-007 | **LLM Model Selection** — File browser to point to local model weights (GGUF, safetensors, GPTQ, EXL2). Validate VRAM requirement. | Model path injected into hive_mind init config; validation prevents VRAM overallocation | P0 |
| CONFIG-008 | **MMIO Auto-Calculation** — Based on selected GPU(s) BAR1 sizes, auto-calculate required High MMIO space using formula: `MMIO = 2 × BAR1_Size × GPU_Count`. | Applies `Set-VM -HighMemoryMappedIoSpace` logic inside APBX | P0 |
| CONFIG-009 | **Configuration Summary** — Write all choices out to `symbiose_config.json`. | JSON payload cleanly serializes selected environment for Phase 4 consumption | P0 |

### MODULE: CI — .github/workflows/

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| CI-001 | `forge-apbx.yml` | Add WDK build step (`egor-tensin/setup-wdk`) to compile `symbiose_bridge.sys` alongside mingw-w64 code. | CI produces `symbiose_bridge.sys` + `ChaosLoader.exe` + sealed `.apbx` | P0 |
| CI-002 | `forge-apbx.yml` | Ensure `.apbx` sealing (`7z -t7z -p"malte" -mhe=on`) runs strictly after binaries are built. | `.apbx` opens in AME Wizard Beta with password `malte` | P0 |

### MODULE: BRIDGE — 02_Symbiose_Bridge/ (NATIVE HYPERVISOR)

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| BRIDGE-001 | `src/driver_entry.c` | Implement `DriverEntry` with `WdfDriverCreate` + `WDF_DRIVER_CONFIG`; register `EvtDriverDeviceAdd`. | Driver loads; WinDbg confirms WDF object tree | P0 |
| BRIDGE-002 | `src/symbiose_bridge.c` | Implement `EvtDriverDeviceAdd`: assert PPO via `WdfDeviceInitSetPowerPolicyOwnership(TRUE)` **before** `WdfDeviceCreate`. | Device visible in Device Manager; GUID queryable | P0 |
| BRIDGE-003 | `src/symbiose_bridge.c` | Implement `EvtDevicePrepareHardware`: parse resource list for BAR MMIO + interrupts; `MmMapIoSpace` for BARs. | Kernel debugger shows valid VA per BAR | P0 |
| BRIDGE-004 | `src/symbiose_bridge.c` | For each `CmResourceTypeInterrupt`: `WdfInterruptCreate` with initialized `WDF_INTERRUPT_CONFIG`; assign ISR + DPC. | MSI-X vectors created; no execution level bugcheck | P0 |
| BRIDGE-005 | `src/ioctl_handler.c` | Inverted-call WDFQUEUE: `WdfIoQueueCreate` parallel dispatch; pend async IOCTLs; **release all spinlocks before `WdfRequestComplete`**. | User-mode host wakes <1ms; no deadlock under load | P0 |
| BRIDGE-006 | `src/vmx_hypervisor.c` | Execute `VMXON`, allocate VMCS regions, and construct EPT tables mapping the RAM allocated by the host. | VMX root mode successfully entered | P0 |
| BRIDGE-007 | `src/acpi_handler.c` | Power callbacks: `WdfDeviceAssignS0IdleSettings`; Death Rattle hook (30s timeout on shutdown, waits for LLM `ACK_READY_TO_DIE`). | System survives S3/S0ix; LLM saves state before power off | P1 |
| BRIDGE-008 | `src/acpi_handler.c` | PnP notification: on ARRIVAL offload to `WdfWorkItem`; on QUERY_REMOVE close handle immediately. | No PnP manager deadlock | P1 |
| BRIDGE-009 | `src/nvme_isolation.c` | SymbioseNull filter attachment for NVMe: suppress all `IRP_MJ_*` except power. | Windows loses NVMe visibility; Chaos OS has exclusive block access | P0 |
| BRIDGE-010 | `src/SwitchToChaos.asm` | Assembly thunk: Execute `VMLAUNCH` to transition from VMX root to the guest executing `BZIMAGE`. | Kernel boots; `startup_32` executed inside VMX non-root | P1 |
| BRIDGE-011 | `inf/SymbioseNull.inf` | MSI-X AddReg: `MSISupported=1` + `MessageNumberLimit`; WDF coinstaller. | MSI-X in Device Manager; clean install | P0 |

### MODULE: HIVE-LOADER — 03_HiveMind_Orchestrator/ChaosLoader/

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| HIVE-LOADER-001 | `src/main.c` | Read `symbiose_config.json` for RAM/vCPU/NUMA; allocate process memory buffer for guest RAM. | Host buffer matches RAM config | P0 |
| HIVE-LOADER-002 | `src/kernel_ioctls.c` | Send IOCTL to `symbiose_bridge` to register the allocated RAM buffer into the driver's EPT tables. | KMDF confirms successful EPT mapping | P0 |
| HIVE-LOADER-003 | `src/boot_params.c` | Build Linux Boot Protocol 2.13 zero-page; set `cmd_line_ptr` = `init=/symbiose/hive_mind`. Send structure to KMDF driver. | Boot log shows correct cmdline, initrd, and RAM size | P0 |
| HIVE-LOADER-004 | `src/main.c` | Send final IOCTL to KMDF driver to trigger `VMLAUNCH`. | Driver initiates guest execution | P0 |
| HIVE-LOADER-005 | `src/main.c` | Wait on async IOCTL for VM-Exits. If exit is Triple Fault, dump `CR0`/`CR2`/`CR3`/`RIP` to terminal. | Triple fault dumps diagnostics to Windows terminal | P1 |
| HIVE-LOADER-006 | `src/main.c` | Serial emulation: process intercept IOCTLs from KMDF driver to route guest ttyS0 to Windows console. | Serial output visible in Windows terminal | P0 |

### MODULE: HIVE-IRC — 03_HiveMind_Orchestrator/IRCd_Neural_Bus/

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| HIVE-IRC-001 | `src/symbiose_ircd.c` | IRCv3 server: TCP 127.0.0.1:6667; Handle NICK/USER/JOIN/TAGMSG for MoE channels (`#oracle`, `#recon`, etc). | IRC client connects, joins channels, talks to AI | P0 |
| HIVE-IRC-002 | `src/symbiose_ircd.c` | IRCv3 extensions: negotiate `labeled-response`, `batch`, `message-tags`. | Cap negotiation succeeds; TAGMSG carries metadata | P1 |
| HIVE-IRC-003 | `src/jumbo_payload.c` | 512MB SHM jumbo payload: `CreateFileMapping`; write struct with CRC64; send pointer via TAGMSG. | Infinite token streams pass checksum validation | P1 |
| HIVE-IRC-004 | `src/symbiose_ircd.h` | Shutdown protocol: bridge `SHUTDOWN_IMMINENT` to `#oracle` and reply with `ACK_READY_TO_DIE`. | Death Rattle functions end-to-end | P1 |

### MODULE: HIVE-VFS — 03_HiveMind_Orchestrator/VFS_Storage_Manager/

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| HIVE-VFS-001 | `src/vfs_manager.c` | Kernel SHM window: `WdfMemoryCreate` → MDL → `MmMapLockedPagesSpecifyCache` into user-mode process VA. | User-mode reads/writes kernel buffer without pagefault | P0 |
| HIVE-VFS-002 | `src/vfs_manager.c` | Register SHM via KMDF into the EPTs so Linux driver accesses directly. | Guest writes immediately visible in host kernel buffer | P0 |
| HIVE-VFS-003 | `src/vfs_manager.h` | Implement METHOD_NEITHER NVMe IOCTLs for zero-copy read/writes to CCD drives. | Zero-copy block access confirmed via WPP trace | P1 |

### MODULE: HIVE-MOSIX — 03_HiveMind_Orchestrator/openmosix_nx/

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| HIVE-MOSIX-001 | `src/migrate.c` | Complete migration cycle via CRIU checkpoint + `cudaMemcpy` VRAM serialization + RDMA libibverbs. | Full process migration completes across network | P2 |
| HIVE-MOSIX-002 | `src/bpf_gpu_monitor.bpf.c` | bpftime userspace eBPF uprobes on `cuMemAlloc` and `cuLaunchKernel` inside `libcudart.so`. | GPU events visible in ringbuf with nanosecond precision | P2 |
| HIVE-MOSIX-003 | `src/criugpu_daemon.c` | CRIU plugin: lock APIs, await streams, dump VRAM, restart on target node. | GPU state perfectly survives checkpoint/restore | P2 |
| HIVE-MOSIX-004 | `src/openmosix_tensor.h` | C Header definitions for tensor migration structures and cluster load scoring logic. | Consumed cleanly by migrate.c | P2 |

### MODULE: APBX — 04_APBX_Transmigration/

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| APBX-001 | `playbook/Configuration/main.yml` | Master orchestration logic defining Phase 1 through 4 + `!registryKey` injects for MSI-X support. | Clean AME wizard install sequence | P0 |
| APBX-002 | `playbook/Configuration/Tasks/phase0_config.yml` | Configure the AME UI to run the CONFIG module tasks; save to `symbiose_config.json`. | User UI functions flawlessly | P0 |
| APBX-003 | `playbook/Configuration/Tasks/hardware_airlock.yml` | Implement DDA script: Disable, Dismount, Set-VM (with auto-calc HighMMIO), Add-VM. | Selected hardware correctly unbinds and assigns | P0 |
| APBX-004 | `playbook/Configuration/Tasks/vbs_annihilate.yml` | Implement Group policy registry overrides to destroy VBS, HVCI, Memory Integrity. | Overrides persist across reboot; driver loads | P0 |
| APBX-005 | `playbook/Configuration/Tasks/phase1.yml` | Implement TrustedInstaller execution using `IRegisteredTask::RunEx`. | Protected registry modifications succeed | P0 |
| APBX-006 | `playbook/Configuration/Tasks/phase4_5.yml` | Inject launch args from JSON into ChaosLoader: `--ram`, `--vcpu`, `--mode`, `--init=/symbiose/hive_mind`. | ChaosLoader starts accurately matching user config | P0 |

### MODULE: TEST — 05_Integration_Tests/

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| TEST-001 | `qemu_scripts/phase4_qemu_test.sh` | Dump VMCS registers upon triple fault; validate CR0/CR2/CR3/RIP. | Full register state captured | P1 |
| TEST-002 | `qemu_scripts/irc_bus_test.sh` | End-to-End IRC check: Connect → Labeled TAGMSG → Batch correlation → Jumbo SHM payload test with CRC64. | All channels reachable; payload CRC64 validates | P1 |
| TEST-003 | `qemu_scripts/native_vmx_smoke.ps1` | ChaosLoader smoke test monitoring serial output for kernel init sequence. Fail on 30s timeout. | Passes on clean host; fails predictably on EPT misconfig | P1 |

---

## XII. DEPENDENCY GRAPH

```
CONFIG-001..009 → symbiose_config.json
     ↓
CI-001, CI-002 (build pipeline)
     ↓
BRIDGE-001 → 002 → 003 → 004 → 011
                         → 005 (WDFQUEUE)
                         → 006 (VMXON / EPT mapping)
                         → 007 (ACPI/Death Rattle)
                         → 008 (PnP)
                         → 009 (NVMe isolation)
                         → 010 (VMLAUNCH ASM Thunk)
     ↓
HIVE-LOADER-001 (reads symbiose_config.json)
→ 002 → 003 → 004 → 005
BRIDGE-005 ────────→ HIVE-LOADER-006
     ↓
HIVE-IRC-001 → 002 → 003 → 004
HIVE-VFS-001 → 002 → 003
HIVE-MOSIX-004 → 001 → 002 → 003
     ↓
APBX-001 → 002 (Phase 0 config) → 003 → 004 → 005 → 006
     ↓
TEST-001, 002, 003
     ↓
CI-002 seals .apbx
```

---

## XIII. VERIFICATION PLAN

| Module | Method |
|--------|--------|
| CONFIG | AME Wizard shows interactive UI; `symbiose_config.json` contains valid selections |
| CI | Workflow green; `.apbx` opens in AME Wizard with password `malte` |
| BRIDGE | WinDbg + WPP; Device Manager shows MSI-X; driver executes VMXON without crash |
| HIVE-LOADER | Serial console shows kernel boot from native KMDF hypervisor |
| HIVE-IRC | User connects IRC client to `#oracle` and talks to the AI; AI responds |
| HIVE-VFS | WPP confirms zero-copy; guest writes reflected in host kernel buffer |
| HIVE-MOSIX | Checkpoint/restore cycle; VRAM CRC64 match |
| APBX | Playbook completes on clean Win10/11; user-selected GPU visible in guest |
| TEST | All 3 scripts pass in CI |

---

## XIV. OPENMOSIX LEGACY DATA ARCHIVE (Data Archiving Phase 1-3)

Data extracted from the `CHAOS 1.5` legacy ramdisk mapping clustering behaviors that must be modernized into the IRC/eBPF/RDMA layer.

### XIV·1 Ramdisk Extraction (`CHAOS.RDZ`)
- Compressed Minix V1 Filesystem (magic `0x138f`), 4096 inodes.
- Extracted into `06_OpenMosix_Exploration` for study.
- `/etc/openmosix.map` - Contains static mapping IP addresses to OpenMosix node-numbers.

### XIV·2 Cluster Topology (`omdiscd`)
- Used UDP broadcast/multicast to discover active OpenMosix nodes.
- Updated `/etc/openmosix.map` automatically.
- **Modernization**: Replaced by `#cluster-announce` IRC TAGMSG payloads. UDP/TCP stack is bypassed.

### XIV·3 Procfs Interface (`/proc/hpc/`)
- `/proc/hpc/admin/block` (Prevent migration)
- `/proc/hpc/admin/bring` (Bring home migrated processes)
- `/proc/hpc/admin/expel` (Expel guest processes)
- `/proc/hpc/admin/lstay` / `stay` (Lock processes to local node)
- **Modernization**: Transition logic into the CRIU/RDMA daemon controls.

### XIV·4 The PID 1 (`/sbin/init`)
- Statically linked ELF binary calling `/sbin/acpid`, `/sbin/ntpd -g`, `/sbin/omdiscd`.
- Interacts with `setpe -off` (disables Mosix extensions).
- **Modernization**: The `hive_mind` init handles LLM instantiation and MoE bindings, entirely replacing legacy SysV/OpenMosix init scripts.

---

## XV. HARDWARE INTRINSICS & DATA STRUCTURES

To prevent agent hallucinations during code generation, the following low-level structures and hardware intrinsics must be strictly enforced:

### XV·1 VMX & EPT Structural Definitions
- **MSR Polling:** Before executing `VMXON`, the KMDF driver must poll `IA32_FEATURE_CONTROL` (MSR 0x3A) to ensure bit 2 (VMX outside SMX) is locked and enabled. It must also read `IA32_VMX_BASIC` (MSR 0x480) to determine the VMCS revision identifier and permitted memory types.
- **EPT 4-Level Layout:** EPT initialization requires explicit 64-bit struct definitions for `PML4E`, `PDPTE`, `PDE`, and `PTE`.
  - Flags mandatory for mapping: `ReadAccess` (Bit 0), `WriteAccess` (Bit 1), `ExecuteAccess` (Bit 2), and `MemoryType` (Bits 5:3) mapped to Write-Back (WB = 6).
  - All EPT structures must be allocated in non-paged contiguous memory aligned to a 4096-byte boundary.

### XV·2 VMCS Field Encoding Constants
The KMDF driver must use `__vmx_vmwrite` to configure over 150 VMCS fields. The following critical hexadecimal constants must be explicitly defined:
- `GUEST_CR0` (0x6800), `GUEST_CR3` (0x6802), `GUEST_CR4` (0x6804)
- `GUEST_RIP` (0x681E), `GUEST_RSP` (0x681C)
- `HOST_CR0` (0x6C00), `HOST_CR3` (0x6C02), `HOST_CR4` (0x6C04)
- `HOST_RIP` (0x6C16)
- Execution controls: `CPU_BASED_VM_EXEC_CONTROL` (0x4002), `SECONDARY_VM_EXEC_CONTROL` (0x401E)

### XV·3 Memory Alignment & Struct Padding Strictures
To prevent IPC memory corruption between the Ring-3 `symbiose_ircd.exe` and the Ring-0 `symbiose_bridge.sys`, all shared structs must reject compiler-injected padding:
```c
#pragma pack(push, 1)
typedef struct _SYMBIOSE_JUMBO_PAYLOAD {
    UINT64 Magic;
    UINT64 CRC64;
    UINT32 PayloadLength;
    // ... payload data
} SYMBIOSE_JUMBO_PAYLOAD, *PSYMBIOSE_JUMBO_PAYLOAD;
#pragma pack(pop)
```
- The `boot_params` (Zero Page) for Linux Protocol 2.13 must similarly utilize `#pragma pack(1)` or `__attribute__((packed))` during cross-compilation via `mingw-w64`.

### XV·4 Assembly ABI Contract (`SwitchToChaos.asm`)
The Microsoft x64 calling convention must be strictly preserved during the VMX thunk. The inline or external assembly must save volatile registers (`RCX`, `RDX`, `R8`, `R9`) and allocate the 32-byte shadow space before executing `VMLAUNCH`. Failure to preserve the stack frame will cause the host kernel to instantly bugcheck upon a VM-Exit return.
