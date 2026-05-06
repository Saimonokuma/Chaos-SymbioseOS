**Project ID:** `CHAOS-SYMB-2026-V2`
**Classification:** `KMDF_HYPERVISOR_BRIDGE`
**Status:** `TASK_MATRIX_V2_ACTIVE`
**Base Research:** `Symbiose Bridge KMDF Driver Research Plan.md`
**Repo Audit:** All original T-001–T-018 scaffolding is present but **stub-level only** — no component constitutes a working implementation. This plan redefines all tasks based on KMDF correctness requirements.

---

## REPO AUDIT SUMMARY

| Module | Files Present | Implementation Status |
|--------|--------------|----------------------|
| `02_Symbiose_Bridge/src/` | symbiose_bridge.c/h, acpi_handler.c, driver_entry.c, ioctl_handler.c, nvme_isolation.c, SwitchToChaos.asm | **STUB** — no KMDF framework calls, no WDF object lifecycle |
| `02_Symbiose_Bridge/inf/` | SymbioseNull.inf | **INCOMPLETE** — missing MSI-X registry keys, no interrupt management subkeys |
| `03_HiveMind_Orchestrator/ChaosLoader/` | whpx_boot.c, src/main.c, boot_params.c/h | **STUB** — GPA mapping incomplete, boot protocol 2.13 fields not populated |
| `03_HiveMind_Orchestrator/IRCd_Neural_Bus/` | symbiose_ircd.c/h, jumbo_payload.c | **STUB** — inverted call queue not implemented, no WDFQUEUE object |
| `03_HiveMind_Orchestrator/VFS_Storage_Manager/` | vfs_manager.c/h | **STUB** — MmMapIoSpace/MDL/zero-copy window absent |
| `03_HiveMind_Orchestrator/openmosix_nx/` | migrate.c, bpf_gpu_monitor.bpf.c, criugpu_daemon.c | **STUB** — bpftime integration absent, CRIU plugin hooks missing |
| `03_HiveMind_Orchestrator/OpenMosix_2026/` | openmosix_tensor.h, scripts/ | **STUB** — header only, no migration engine |
| `04_APBX_Transmigration/playbook/` | playbook.conf, main.yml, Tasks/*.yml | **PARTIAL** — phase YAMLs exist but DDA PowerShell, MSI-X reg keys, and Group Policy overrides absent |
| `.github/workflows/forge-apbx.yml` | forge-apbx.yml | **FUNCTIONAL** — CI pipeline structurally correct |
| `05_Integration_Tests/` | phase4_qemu_test.sh | **STUB** — no triple-fault recovery or vCPU register validation |

---

## ARCHITECTURE DECISION RECORD

**KMDF over UMDF2** — Non-negotiable. UMDF2 cannot handle DMA, METHOD_NEITHER buffering, or DIRQL interrupt dispatch. All driver code must target KMDF (WDF 1.31+) compiled via the WDK.

**WHPX over Hyper-V full-stack** — ChaosLoader uses Windows Hypervisor Platform (WHPX) APIs directly. Max 64 vCPUs per partition on Windows 11 24H2.

**MSI-X over INTx** — All interrupt vectors must be MSI-X. INTx is deprecated across the entire bridge.

**Inverted Call IPC** — The WHPX host process pre-queues async IOCTLs into a WDFQUEUE. The KMDF ISR/DPC completes them on hardware events. No kernel-to-usermode upcalls.

**Zero-Copy Shared Memory** — WdfMemoryCreate + MDL + MmMapLockedPagesSpecifyCache exposes kernel memory directly into WHPX host process VA space, then registered as WHPX GPA backing.

**bpftime for GPU Observability** — Userspace eBPF via bpftime attaches uprobes to libcudart.so/libcuda.so without kernel eBPF overhead.

**CRIU + GPU plugins for migration** — cudaMemcpy / RDMA libibverbs serialize VRAM state on checkpoint.

---

## TASK MATRIX V2

### LAYER 0 — Build Infrastructure

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| L0-001 | `.github/workflows/forge-apbx.yml` | Add WDK cross-compile step using `egor-tensin/setup-wdk` action; produce `symbiose_bridge.sys` artifact | CI produces signed `.sys` and `.inf` artifacts | P0 |
| L0-002 | `CMakeLists.txt` (root) | Separate WDK targets from mingw-w64 targets; WDK builds `.sys`, mingw builds `.exe` | `cmake --build` produces both target classes without collision | P0 |
| L0-003 | `02_Symbiose_Bridge/CMakeLists.txt` | Add KMDF WDF coinstaller linkage, WPP tracing macros, NASM for SwitchToChaos.asm | Driver binary links WdfCoInstaller correctly | P0 |

### LAYER 1 — KMDF Driver Core (`02_Symbiose_Bridge`)

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| L1-001 | `src/driver_entry.c` | Implement `DriverEntry` with `WdfDriverCreate`, set `EvtDriverDeviceAdd` callback; configure `WDF_DRIVER_CONFIG` | Driver loads without bugcheck; WinDbg confirms WDF object created | P0 |
| L1-002 | `src/symbiose_bridge.c` | Implement `EvtDriverDeviceAdd`: create WDFDEVICE, assert PPO via `WdfDeviceInitSetPowerPolicyOwnership(TRUE)` before `WdfDeviceCreate`; register device interface GUID | Device appears in Device Manager; interface GUID queryable | P0 |
| L1-003 | `src/symbiose_bridge.c` | Implement `EvtDevicePrepareHardware` / `EvtDeviceReleaseHardware`: parse `WdfCmResourceListGetDescriptor` for BAR MMIO and interrupt resources; call `MmMapIoSpace` for BAR regions | Resources mapped; kernel debugger shows valid VA for each BAR | P0 |
| L1-004 | `src/acpi_handler.c` | Implement power policy callbacks via `WdfDeviceInitSetPowerPolicyEventCallbacks`; implement `EvtDeviceArmWakeFromS0` / `EvtDeviceDisarmWakeFromS0`; call `WdfDeviceAssignS0IdleSettings` with `IdleCanWakeFromS0` and D3cold (KMDF 1.11+) | System survives S3/S0ix cycle; driver re-arms correctly on resume | P1 |
| L1-005 | `src/acpi_handler.c` | Implement PnP device interface change notification; on ARRIVAL offload handle open to `WdfWorkItem` to avoid PnP manager deadlock; on QUERY_REMOVE close handle immediately | No PnP deadlock under stress plug/unplug | P1 |
| L1-006 | `src/ioctl_handler.c` | Implement inverted call WDFQUEUE: `WdfIoQueueCreate` with parallel dispatch for async poll IOCTLs; pend incoming IRPs; complete from ISR/DPC context via `WdfRequestComplete`; release all spinlocks before completion | User-mode WHPX process wakes within 1ms of hardware event; no deadlock under load | P0 |
| L1-007 | `src/ioctl_handler.c` | Implement IRCv3-style labeled-response correlation: embed unique tag in each pending IRP output buffer; implement batch start/end markers for fragmented Scatter/Gather completions | WHPX host correctly reassembles multi-IRP transactions; no torn reads in guest | P1 |
| L1-008 | `src/nvme_isolation.c` | Implement SymbioseNull filter driver attachment for NVMe controllers; suppress all IRP_MJ_* passthrough except power | Windows NTFS loses visibility of target NVMe; Chaos-OS has exclusive block access | P0 |
| L1-009 | `src/SwitchToChaos.asm` | Complete identity-mapping thunk: map physical page containing `mov cr3, rax` identically in VA; clear PG bit; clear LME in EFER MSR; far jump to 32-bit compat CS before BZIMAGE entry | No triple fault during mode switch; kernel reaches startup_32 | P1 |

### LAYER 1B — INF and MSI-X (`02_Symbiose_Bridge/inf`)

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| L1B-001 | `inf/SymbioseNull.inf` | Add `Interrupt Management\MessageSignaledInterruptProperties` AddReg: `MSISupported=1` (REG_DWORD); set `MessageNumberLimit` to required vector count | Device Manager shows MSI-X vectors allocated; no INTx fallback | P0 |
| L1B-002 | `inf/SymbioseNull.inf` | Add WDF coinstaller CopyFiles section and `KmdfService` AddService directive with correct `KmdfLibraryVersion` | Driver installs without inf2cat errors on Win10/11 | P0 |
| L1B-003 | `src/symbiose_bridge.c` | In `EvtDevicePrepareHardware`, iterate `CmResourceTypeInterrupt` descriptors; call `WdfInterruptCreate` for each MSI-X vector with zeroed `WDF_INTERRUPT_CONFIG` (init via macro); assign `EvtInterruptIsr` and `EvtInterruptDpc`; respect execution level contract | All MSI-X vectors created without STATUS_WDF_INCOMPATIBLE_EXECUTION_LEVEL bugcheck | P0 |
| L1B-004 | `src/symbiose_bridge.c` | In MSI-X ISR: signal WHPX partition event handle; in DPC: retrieve pending IRP, populate output buffer, call `WdfRequestComplete`; release all spinlocks before completion | Sub-millisecond user-mode wakeup via ETW; no deadlock under load | P0 |

### LAYER 2 — ChaosLoader / WHPX Bootstrap (`03_HiveMind_Orchestrator/ChaosLoader`)

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| L2-001 | `src/main.c` | Implement full WHPX partition lifecycle: `WHvCreatePartition` to `WHvSetPartitionProperty` (ProcessorCount <= 64) to `WHvSetupPartition`; handle HRESULT errors | Partition created; WHvGetCapability confirms WHPX available | P0 |
| L2-002 | `whpx_boot.c` | Implement `WHvMapGpaRange` for all guest regions: kernel at 1MB physical, initrd above kernel, zero page at 0x10000; apply `WHvMapGpaRangeFlagExecute` on code regions; enforce 4096-byte alignment | Kernel code mapped executable; no access violation on first vCPU fetch | P0 |
| L2-003 | `src/boot_params.c` | Implement full Linux Boot Protocol 2.13 zero-page: populate setup_header at correct byte offsets; set `cmd_line_ptr` (0x0228), `ramdisk_image` (0x0218), `ramdisk_size` (0x021C); check `xloadflags` bit 1 for above-4GB mapping; inject e820 memory map | Kernel boot log shows correct cmdline and initrd; no early boot panic | P0 |
| L2-004 | `src/main.c` | Set initial vCPU register state via `WHvSetVirtualProcessorRegisters`: RIP=0x100000, RSI=zero_page_GPA, CS with 64-bit attributes, CR0 PE+PG, CR4 PAE; launch `WHvRunVirtualProcessor` per vCPU thread | vCPU executes kernel startup; serial console output visible | P0 |
| L2-005 | `src/main.c` | Implement exit reason handler loop for `WHvRunVpExitReasonX64IoPortAccess` and `WHvRunVpExitReasonException` (triple fault); on triple fault call `WHvGetVirtualProcessorRegisters` for CR0/CR2/CR3/RIP dump; call `WHvTranslateGva` for GVA-to-GPA | Triple fault produces diagnostic dump instead of silent hang | P1 |
| L2-006 | `src/main.c` | Open symbiose_bridge device handle; send configuration IOCTL to retrieve shared memory window GPA; register that window into WHPX via `WHvMapGpaRange` | Guest reads/writes KMDF shared memory at mapped GPA without VM-Exit | P0 |

### LAYER 3 — IRCd Neural Bus (`03_HiveMind_Orchestrator/IRCd_Neural_Bus`)

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| L3-001 | `src/symbiose_ircd.c` | Implement IRCv3 server core: TCP listen on 127.0.0.1:6667; NICK/USER registration; channel JOIN/PRIVMSG/TAGMSG routing for all 5 channels | IRC client can connect, join all channels, exchange messages | P0 |
| L3-002 | `src/symbiose_ircd.c` | Implement IRCv3 extensions: `labeled-response` (unique tag echo), `batch` (start/end markers), `message-tags` (arbitrary k=v in TAGMSG) | IRC client with cap negotiation receives labeled/batched responses correctly | P1 |
| L3-003 | `jumbo_payload.c` | Complete SHM jumbo payload: CreateFileMapping/MapViewOfFile for 512MB region; write payload + CRC64; send pointer TAGMSG | Payloads greater than 512KB transferred via SHM pointer; receiver validates CRC64 | P1 |
| L3-004 | `src/symbiose_ircd.h` | Define shutdown signal protocol: `SHUTDOWN_IMMINENT` TAGMSG on #oracle; `ACK_READY_TO_DIE` response triggers KMDF ACPI power callback release within 30s | ACPI intercept test: driver waits for ACK before allowing power off | P1 |

### LAYER 4 — VFS Storage Manager (`03_HiveMind_Orchestrator/VFS_Storage_Manager`)

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| L4-001 | `src/vfs_manager.c` | Implement kernel shared memory window: `WdfMemoryCreate`; build MDL via `IoAllocateMdl` + `MmBuildMdlForNonPagedPool`; map into requesting process VA via `MmMapLockedPagesSpecifyCache` | User-mode process reads/writes kernel buffer at returned VA without pagefault | P0 |
| L4-002 | `src/vfs_manager.c` | Register shared memory window as WHPX GPA backing via `WHvMapGpaRange`; guest Linux driver reads/writes without VM-Exit | Guest write visible in host kernel buffer; no VMEXIT overhead | P0 |
| L4-003 | `src/vfs_manager.h` | Define NVMe direct-access IOCTLs using METHOD_NEITHER buffering for zero-copy | IOCTL round-trip completes without buffer copy; validated via WPP trace | P1 |

### LAYER 5 — OpenMosix NX / GPU Migration (`03_HiveMind_Orchestrator/openmosix_nx`)

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| L5-001 | `migrate.c` | Complete checkpoint_and_migrate: CRIU dump; VRAM serialization via `cudaMemcpy`; RDMA stream via `libibverbs ibv_post_send`; CRIU restore on target | Process checkpoint and VRAM dump completes; restore boots on target node | P2 |
| L5-002 | `bpf_gpu_monitor.bpf.c` | Integrate bpftime runtime: attach uprobe to `cuMemAlloc` and `cuLaunchKernel` in libcudart.so; pipe events into BPF_MAP_TYPE_RINGBUF | GPU alloc/launch events visible in ring buffer without kernel eBPF; nanosecond timestamps confirmed | P2 |
| L5-003 | `criugpu_daemon.c` | Implement CRIU plugin hooks: lock driver APIs, await stream completion, force VRAM dump; implement restore path that reinitializes CUDA context | Full GPU state survives checkpoint/restore cycle | P2 |
| L5-004 | `OpenMosix_2026/src/openmosix_tensor.h` | Define `tensor_migration_request_t` and cluster node scoring struct with GPU thermal, VRAM free, inference queue depth | Header compiles cleanly; structs used by migrate.c and criugpu_daemon.c | P2 |

### LAYER 6 — APBX Playbook (`04_APBX_Transmigration`)

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| L6-001 | `playbook/Configuration/main.yml` | Add `!registryKey` actions for MSI-X subkeys: `Interrupt Management\MessageSignaledInterruptProperties\MSISupported=1` and `MessageNumberLimit` | Registry keys present after playbook run; device shows MSI-X | P0 |
| L6-002 | `playbook/Configuration/Tasks/hardware_airlock.yml` | Replace generic pnputil calls with proper DDA sequence via `!powerShell`: `Disable-PnpDevice`, `Dismount-VMHostAssignableDevice -Force`, `Set-VM -HighMemoryMappedIoSpace 33280Mb`, `Add-VMAssignableDevice` | GPU/NVMe assigned to WHPX partition; no Code 12 resource error in guest | P0 |
| L6-003 | `playbook/Configuration/Tasks/vbs_annihilate.yml` | Add persistent Group Policy override keys under `HKLM\SOFTWARE\Policies\Microsoft\Windows\DeviceGuard` and `HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard`; ensure settings survive reboot | VBS/HVCI remains disabled after cold reboot; KMDF driver loads at Ring-0 | P0 |
| L6-004 | `playbook/Configuration/Tasks/phase1.yml` | Add TrustedInstaller execution path via `IRegisteredTask::RunEx` COM with `NT SERVICE\TrustedInstaller` user string | Phase 1 actions execute as TrustedInstaller; no access denied on protected keys | P0 |
| L6-005 | `playbook/Configuration/Tasks/phase4_5.yml` | Update ChaosLoader launch args: add `--whpx-virt`, `--max-vcpu 64`, `--high-mmio 33280`, `--boot-protocol 2.13` | ChaosLoader launches correctly; kernel boot log confirms protocol 2.13 handoff | P0 |

### LAYER 7 — Integration Tests (`05_Integration_Tests`)

| ID | File | Task | Acceptance Criteria | Priority |
|----|------|------|---------------------|----------|
| L7-001 | `qemu_scripts/phase4_qemu_test.sh` | Add vCPU register dump after triple fault exit; validate CR0/CR2/CR3/RIP against expected identity-mapped values | Test prints full register state on fault; CI catches regressions | P1 |
| L7-002 | `05_Integration_Tests/irc_bus_test.sh` | Connect IRC client to symbiose_ircd; send labeled TAGMSG; verify batch response correlation; test jumbo payload SHM transfer with CRC64 validation | All 5 IRC channels reachable; labeled-response tags match; CRC64 validates | P1 |
| L7-003 | `05_Integration_Tests/whpx_boot_smoke.ps1` | PowerShell script launches ChaosLoader, monitors serial output for kernel init string, fails if triple fault detected within 30s | Smoke test passes on clean host; fails predictably on misconfigured MMIO | P1 |

---

## DEPENDENCY GRAPH

```
L0-001, L0-002, L0-003  (Build Infrastructure)
    └── L1-001 (DriverEntry)
        └── L1-002 (DeviceAdd + PPO)
            ├── L1-003 (PrepareHardware + BAR mapping)
            │   ├── L1B-003 (MSI-X WdfInterruptCreate)
            │   │   └── L1B-004 (ISR/DPC + inverted call)
            │   │       └── L1-006 (WDFQUEUE inverted call)
            │   │           └── L1-007 (labeled-response IPC)
            │   └── L1-008 (NVMe isolation filter)
            ├── L1-004 (Power policy + D3cold)
            ├── L1-005 (PnP interface notification)
            └── L1B-001, L1B-002 (INF MSI-X keys)
L1-006 ──── L2-006 (KMDF to WHPX shared memory bridge)
L2-001 → L2-002 → L2-003 → L2-004 → L2-005
L4-001 → L4-002 → L4-003
L3-001 → L3-002 → L3-003 → L3-004
L5-004 → L5-001 → L5-002 → L5-003
L6-001 → L6-002 → L6-003 → L6-004 → L6-005
L7-001, L7-002, L7-003 (require all L1-L6 complete)
```

---

## CRITICAL ENGINEERING CONSTRAINTS

1. **PPO assertion** — `WdfDeviceInitSetPowerPolicyOwnership(TRUE)` MUST be called before `WdfDeviceCreate`. Failure causes bugcheck.
2. **Spinlock release before completion** — All locks must be released before `WdfRequestComplete` on sequential queues. Failure causes hard deadlock.
3. **PnP ARRIVAL offload** — Device handle open on ARRIVAL must use `WdfWorkItem`. Direct open in callback causes PnP manager deadlock.
4. **Execution level contract** — If parent executes at `WdfExecutionLevelPassive`, supply `EvtInterruptWorkItem` NOT `EvtInterruptDpc`. Violation causes `STATUS_WDF_INCOMPATIBLE_EXECUTION_LEVEL` bugcheck.
5. **WHvMapGpaRange alignment** — All GPA regions must be 4096-byte aligned. Misalignment returns `E_INVALIDARG`.
6. **Execute flag mandatory** — `WHvMapGpaRangeFlagExecute` required on kernel and initrd regions. Omission causes instant vCPU access violation.
7. **vCPU limit** — Max 64 vCPUs on Windows 11 24H2 WHPX. Exceeding limit causes partition creation failure.
8. **MMIO scaling** — Single A100/H100 GPU requires `Set-VM -HighMemoryMappedIoSpace 33280Mb` minimum. Without this the guest shows Code 12.
9. **VBS persistence** — Group Policy keys required to survive reboot. Run-once registry changes are insufficient on Win11 24H2.
10. **MSI-X INF keys** — `MSISupported=1` in `Interrupt Management\MessageSignaledInterruptProperties` is mandatory. Missing causes OS to fall back to single legacy interrupt silently.

---

## VERIFICATION PLAN

| Layer | Verification Method |
|-------|---------------------|
| L0 Build | CI green; `symbiose_bridge.sys` and `ChaosLoader.exe` artifacts produced |
| L1 KMDF Driver | WinDbg kernel attach; WPP trace; Device Manager shows device with MSI-X vectors |
| L2 WHPX Boot | Serial console shows Linux kernel boot messages; `whpx_boot_smoke.ps1` passes |
| L3 IRCd | `irc_bus_test.sh` passes all channel, labeled, batch, and jumbo tests |
| L4 VFS/SHM | WPP trace confirms METHOD_NEITHER zero-copy; guest write visible in host kernel |
| L5 Migration | Checkpoint and restore cycle completes; VRAM CRC64 matches pre and post migration |
| L6 APBX | Playbook runs on clean Win10/11 VM; all phases complete; GPU visible in guest |
| L7 Integration | All 3 test scripts pass in CI on ubuntu-24.04 runner with QEMU |
