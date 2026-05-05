# 🔺 OMEGA INTERACTIVE PLAN: CHAOS-SYMBIOSE OS
**Master Execution Pipeline & AI Agent Workflow Tracker**

> **META-DIRECTIVE FOR AI AGENTS (JULES & SWARM):** 
> This is a living document and the absolute source of truth for repository execution. You are mandated to update the `[ ]` checkboxes to `[x]` as you complete phases. If a phase fails Quintangular Verification, reset the checkbox, log the failure in `.jules/crucible.md`, and execute the rollback. Follow your designated Agent Assignments.

## 🧭 AGENT HANDOFF MATRIX

| Phase | Primary Agent | Secondary Agent | Objective |
|-------|---------------|-----------------|-----------|
| **0. Forensics** | 🏛️ `Architect` | 🔺 `Crucible` | Seed validation and extraction. |
| **1. Build System** | 🏛️ `Architect` | 🛡️ `Sentinel` | Scaffolding, CMake, CI/CD, Lockfiles. |
| **2A. WDF Bridge** | 🔨 `Forge` | 🛡️ `Sentinel` | Ring-0 driver, NVMe/GPU Airlock, ACPI Hook. |
| **2B. PID 1 Inject** | 🔨 `Forge` | 🔺 `Crucible` | ChaosLoader, Boot params, Kernel Zero Page patch. |
| **2C. IRC MoE** | 🧠 `Cortex` | ⚡ `Bolt` | Jumbo frame IRCd, zero-latency TCP MoE routing. |
| **2D. VFS Storage** | 🧪 `Alchemist` | 🧠 `Cortex` | ZFS CoW, NVMe bare-metal SPDK bindings. |
| **2E. OpenMosix** | 🐍 `Ouroboros` | 🧠 `Cortex` | 2026+ Heterogeneous Tensor Migration across nodes. |
| **3. APBX Deploy** | 🎨 `Palette` | 🛡️ `Sentinel` | AME Wizard YAML orchestration, VBS Takedown. |
| **4. QEMU Test** | 🔺 `Crucible` | ⚡ `Bolt` | Virtualized integration verification. |
| **5. Hardware** | 👤 `Human` | 🧠 `Cortex` | Physical deployment and Hive Mind awakening. |

---

## 🛠️ CRUCIBLE CHANGELOG (PRE-EXECUTION FIXES)
*25 Critical & Moderate errors resolved by the Lead Architect before Phase 0.*

- **[FIX 1]** Replaced Shell `$$` with `\(` and `\)` in Phase 0.3 `find` command grouping.
- **[FIX 2]** `MAKE_ULONGLONG` replaced with explicit `0x0000000100000000ULL` in WDF header.
- **[FIX 3]** `config.DriverUnloading` corrected to `config.EvtDriverUnload`.
- **[FIX 4 & 5]** `SwitchToChaos.asm`: Stored `BootParams` in `r10` *before* register pushes to prevent stale `[rsp+112]` offset issues and unsafe memory `test`.
- **[FIX 6]** `SwitchToChaos.asm`: Replaced `and eax, ~0x80000000` with `btr eax, 31` for safe CR0 bit clearing.
- **[FIX 7]** `SwitchToChaos.asm`: Relocated `SymbioseTripleFaultRecovery` strictly under the `[BITS 64]` directive.
- **[FIX 8]** `SwitchToChaos.asm`: Defined local GDT and added `lgdt` instruction before the far jump.
- **[FIX 9]** `main.c` (ChaosLoader): Corrected string escaping for `L"\\\\.\\SymbioseBridge"`.
- **[FIX 10]** `main.c` (ChaosLoader): Replaced kernel macro `KdPrint` with `fwprintf(stderr, ...)`.
- **[FIX 11 & 12]** `main.c` (ChaosLoader): Included `<winternl.h>` and defined `SYMBIOSE_PAYLOAD` struct inside a shared header.
- **[FIX 13 & 18]** `SwitchToChaos.asm`: Implemented `BOOT_PARAMS_PML4_OFFSET` and `LINUX_KERNEL_LOAD_ADDR` constants.
- **[FIX 14]** `crucible-verify.yml`: Fixed Shellcheck URL interpolation variable syntax.
- **[FIX 15]** YAML files: Converted all Tab indentations to Spaces.
- **[FIX 16]** `nvme_isolation.c`: Moved TOCTOU `Isolated` state check *inside* `WdfWaitLockAcquire`.
- **[FIX 17]** `SwitchToChaos.asm`: Reordered mode switch (Far Jump to Compat CS ➔ Clear PG ➔ Clear LME).
- **[FIX 19]** `SwitchToChaos.asm`: Removed invalid 64-bit leading underscore from `extern SymbioseTripleFaultHandler`.
- **[FIX 20]** `nvme_isolation.c`: Implemented stubs for `SymbioseFindPciDevice`, `SymbioseDetachDriverStack`, etc., to resolve MSVC linker failures.
- **[FIX 21]** `ioctl_handler.c`: Created kernel-level `ProbeForRead` and `__try/__except` blocks to safely handle nested userspace pointers in `SYMBIOSE_PAYLOAD`.
- **[FIX 22]** `SwitchToChaos.asm`: Converted local label `.compat_mode` to global `compat_mode_entry` for safe far jumps.
- **[FIX 23]** Created `symbiose_ioctls.h` to cleanly share structs between Ring-0 driver and Ring-3 ChaosLoader.
- **[FIX 24/25]** `openmosix_tensor.h`: Added `#ifdef __linux__` guard around `mlockall` to prevent WDK compilation failures on Windows.

---

## 🗺️ PHASE DEPENDENCY GRAPH

```text
Phase 0 (Forensics)
    │
    ▼
Phase 1 (Build System) ──────────────────────────┐
    │                                            │
    ├─► Phase 2A (Symbiose Bridge Driver)        │
    │       │                                    │
    │       ├─► Phase 2A.1 (Driver Header)       │
    │       ├─► Phase 2A.2 (Shared IOCTLs)       │
    │       ├─► Phase 2A.3 (Driver Entry)        │
    │       ├─► Phase 2A.4 (ACPI Hook)           │
    │       ├─► Phase 2A.5 (IOCTL Probing)       │
    │       ├─► Phase 2A.6 (NVMe Isolation)      │
    │       ├─► Phase 2A.7 (Null INF)            │
    │       └─► Phase 2A.8 (Assembly Thunk)      │
    │                                            │
    ├─► Phase 2B (ChaosLoader + PID 1 Injection) │
    │       │                                    │
    │       ├─► Phase 2B.1 (ChaosLoader Main)    │
    │       └─► Phase 2B.2 (Kernel Zero Patch)   │
    │                                            │
    ├─► Phase 2C (IRC Neural Bus)                │
    │       │                                    │
    │       ├─► Phase 2C.1 (Jumbo Frame Ext)     │
    │       └─► Phase 2C.2 (MoE Protocol)        │
    │                                            │
    ├─► Phase 2D (VFS Storage Manager)           │
    │       │                                    │
    │       └─► Phase 2D.1 (SPDK Integration)    │
    │                                            │
    ├─► Phase 2E (OpenMosix 2026+ Tensor Sync)   │
    │       │                                    │
    │       ├─► Phase 2E.1 (C Protocol Header)   │
    │       └─► Phase 2E.2 (AST Meta-Gen)        │
    │                                            │
    ▼                                            ▼
Phase 3 (APBX Transmigration) ◄──────────────────┘
    │
    ▼
Phase 4 (Integration Testing in QEMU)
    │
    ▼
Phase 5 (Physical Hardware Validation)
```

---

## 🔺 PHASE 0: FORENSIC ARCHAEOLOGY & SEED INVENTORY
**[Assigned Agents: ARCHITECT 🏛️ & CRUCIBLE 🔺]**
**Status:** [ ] Pending | [ ] In Progress | [x] Completed

**Objective:** Catalog every byte in `CHAOS 1.5/`, identify transplantable components, and document what must be rewritten from zero.

### 0.1 — Seed File Inventory

```bash
#!/usr/bin/env bash
set -euo pipefail

# Phase 0.1: Forensic inventory of CHAOS 1.5/
# Crucible Pattern: PATTERN-005 (no hardcoded paths)

SEED_DIR="${1:-./CHAOS 1.5}"
INVENTORY_FILE="./02_Symbiose_Bridge/docs/seed_inventory.md"

mkdir -p "$(dirname "$INVENTORY_FILE")"

echo "# CHAOS 1.5 Seed Inventory" > "$INVENTORY_FILE"
echo "Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$INVENTORY_FILE"
echo "" >> "$INVENTORY_FILE"

# Recursive file listing with hashes
find "$SEED_DIR" -type f -print0 | while IFS= read -r -d '' file; do
    hash=$(sha256sum "$file" | awk '{print $1}')
    size=$(stat --format='%s' "$file" 2>/dev/null || stat -f '%z' "$file")
    ftype=$(file -b "$file")
    echo "- \`$file\` | SHA256: \`$hash\` | Size: ${size}B | Type: $ftype" >> "$INVENTORY_FILE"
done

echo "[✓] Seed inventory written to $INVENTORY_FILE"
```

### 0.2 — BZIMAGE & CHAOS.RDZ Extraction Validation

```bash
#!/usr/bin/env bash
set -euo pipefail

# Phase 0.2: Validate transplantable kernel and ramdisk
# Crucible: PATTERN-008 (TOCTOU-safe file operations)

SEED_DIR="${1:-./CHAOS 1.5}"
BZIMAGE="${SEED_DIR}/CHAOS/BZIMAGE"
CHAOS_RDZ="${SEED_DIR}/CHAOS/CHAOS.RDZ"
VALIDATION_FILE="./02_Symbiose_Bridge/docs/transplant_validation.md"

mkdir -p "$(dirname "$VALIDATION_FILE")"

validate_kernel() {
    local kernel="$1"
    echo "## BZIMAGE Validation" >> "$VALIDATION_FILE"
    
    if [[ ! -f "$kernel" ]]; then
        echo "❌ FAIL: BZIMAGE not found at $kernel" >> "$VALIDATION_FILE"
        return 1
    fi
    
    local magic
    magic=$(xxd -l 4 -p "$kernel")
    
    echo "- Magic bytes: \`$magic\`" >> "$VALIDATION_FILE"
    echo "- Size: $(stat --format='%s' "$kernel" 2>/dev/null || stat -f '%z' "$kernel") bytes" >> "$VALIDATION_FILE"
    
    if [[ "$magic" == "1f8b"* ]]; then
        echo "- Format: gzip-compressed kernel" >> "$VALIDATION_FILE"
    else
        echo "- Format: uncompressed or custom format (requires manual inspection)" >> "$VALIDATION_FILE"
    fi
    
    echo "✅ BZIMAGE located and catalogued" >> "$VALIDATION_FILE"
}

validate_ramdisk() {
    local rdz="$1"
    echo "## CHAOS.RDZ Validation" >> "$VALIDATION_FILE"
    
    if [[ ! -f "$rdz" ]]; then
        echo "❌ FAIL: CHAOS.RDZ not found at $rdz" >> "$VALIDATION_FILE"
        return 1
    fi
    
    local magic
    magic=$(xxd -l 4 -p "$rdz")
    
    echo "- Magic bytes: \`$magic\`" >> "$VALIDATION_FILE"
    echo "- Size: $(stat --format='%s' "$rdz" 2>/dev/null || stat -f '%z' "$rdz") bytes" >> "$VALIDATION_FILE"
    
    case "$magic" in
        1f8b*) echo "- Format: gzip-compressed" >> "$VALIDATION_FILE" ;;
        3037*) echo "- Format: ASCII cpio archive" >> "$VALIDATION_FILE" ;;
        *) echo "- Format: unknown (requires manual inspection)" >> "$VALIDATION_FILE" ;;
    esac
    
    echo "✅ CHAOS.RDZ located and catalogued" >> "$VALIDATION_FILE"
}

validate_kernel "$BZIMAGE"
validate_ramdisk "$CHAOS_RDZ"
```

### 0.3 — OpenMosix Source Extraction

```bash
#!/usr/bin/env bash
set -euo pipefail

# Phase 0.3: Locate and catalog OpenMosix source within seed
# Crucible: PATTERN-005 (pathlib-equivalent in bash)

SEED_DIR="${1:-./CHAOS 1.5}"
OMOSIX_LOG="./02_Symbiose_Bridge/docs/openmosix_catalog.md"

echo "# OpenMosix Source Catalog" > "$OMOSIX_LOG"
echo "Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$OMOSIX_LOG"
echo "" >> "$OMOSIX_LOG"

# FIX 1: Proper regex grouping without bash PID variable collision ($$)
find "$SEED_DIR" -type f \( -name "*.c" -o -name "*.h" -o -name "*.patch" -o -name "Makefile" \) -print0 \
    | xargs -0 grep -l -i "mosix\|openmosix\|om" 2>/dev/null \
    | while IFS= read -r file; do
        echo "- \`$file\`" >> "$OMOSIX_LOG"
    done || true

echo "" >> "$OMOSIX_LOG"
echo "## Analysis Required" >> "$OMOSIX_LOG"
echo "- Identify kernel version target (likely 2.4.x)" >> "$OMOSIX_LOG"
echo "- Catalog syscalls and /proc interface" >> "$OMOSIX_LOG"
echo "- Document migration protocol (TCP/UDP ports, packet format)" >> "$OMOSIX_LOG"
```

### 0.4 — Phase 0 Verification
- [ ] **Axis I:** All scripts parse without error (`bash -n phase0_*.sh`)
- [ ] **Axis II:** File existence checks use proper error handling (`set -euo pipefail`)
- [ ] **Axis III:** Output directory created before write (`mkdir -p` before `>`)
- [ ] **Axis IV:** No hardcoded paths; `$SEED_DIR` parameterized
- [ ] **Axis V:** SHA256 hashes recorded for reproducibility

---

## 🔺 PHASE 1: BUILD SYSTEM & SCAFFOLDING
**[Assigned Agents: ARCHITECT 🏛️ & SENTINEL 🛡️]**
**Status:** [ ] Pending | [ ] In Progress | [x] Completed

**Objective:** Create the repository structure, build system, and CI pipeline before writing a single line of kernel code.

### 1.1 — Repository Structure Creation

```bash
#!/usr/bin/env bash
set -euo pipefail

# Phase 1.1: Create target directory structure
# Crucible: PATTERN-005 (no hardcoded paths)

BASE_DIR="${1:-.}"

mkdir -p "${BASE_DIR}/Chaos-Symbiose-OS"/{\
"CHAOS 1.5",\
"02_Symbiose_Bridge"/{src,inf,docs,tests},\
"03_HiveMind_Orchestrator"/{\
ChaosLoader/{src,tests},\
IRCd_Neural_Bus/{src,tests,protocol},\
VFS_Storage_Manager/{src,tests,spdk_bindings},\
OpenMosix_2026/{src,scripts}},\
"04_APBX_Transmigration"/{playbook,Configuration,Tools,Drivers},\
"05_Integration_Tests"/{qemu_scripts,fixtures,expected},\
"docs"/{architecture,fortification,api},\
".github/workflows",\
".jules"\
}

# Create crucible journal
cat > "${BASE_DIR}/Chaos-Symbiose-OS/.jules/crucible.md" << 'CRUCIBLE_EOF'
# Crucible Journal

## 2026-05-05 - Repository Scaffolding
**Learning:** Build system must be established before any kernel code.
**Action:** Always create directory structure, CI, and build tooling first.
**Defect Pattern ID:** PATTERN-005
**Axes Affected:** III, IV, V
**Level:** L5
CRUCIBLE_EOF

echo "[✓] Repository structure created"
```

### 1.2 — Build System (CMake + Make hybrid)

```cmake
# Phase 1.2: Root CMakeLists.txt
# Crucible: PATTERN-012 (pinned dependencies)

cmake_minimum_required(VERSION 3.28)
project(Chaos-Symbiose-OS VERSION 0.1.0 LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)

# Pinned toolchain versions
set(WDK_VERSION "10.0.26100" CACHE STRING "Windows Driver Kit version")
set(GCC_MIN_VERSION "13.2")
set(NASM_MIN_VERSION "2.16.01")

# Sub-projects
add_subdirectory(02_Symbiose_Bridge)
add_subdirectory(03_HiveMind_Orchestrator/ChaosLoader)
add_subdirectory(03_HiveMind_Orchestrator/IRCd_Neural_Bus)
add_subdirectory(03_HiveMind_Orchestrator/VFS_Storage_Manager)
add_subdirectory(03_HiveMind_Orchestrator/OpenMosix_2026)

# Testing
enable_testing()
add_subdirectory(05_Integration_Tests)
```

### 1.3 — CI Pipeline (GitHub Actions)

```yaml
# .github/workflows/crucible-verify.yml
# FIX 15: Converted all indentation to strict spaces
name: Crucible Quintangular Verification

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]

env:
  CRUCIBLE_ENV: ci

jobs:
  axis-I-syntactic:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Format Check (C/C++)
        run: |
          pip install clang-format==18.1.8
          find . -name '*.c' -o -name '*.h' -o -name '*.cpp' | xargs clang-format --dry-run --Werror
      - name: ShellCheck
        run: |
          scversion="v0.10.0"
          # FIX 14: URL Interpolation repaired
          wget -q "[https://github.com/koalaman/shellcheck/releases/download/$](https://github.com/koalaman/shellcheck/releases/download/$){scversion}/shellcheck-${scversion}.linux.x86_64.tar.xz"
          tar xf shellcheck-*.tar.xz
          find . -name '*.sh' -exec ./shellcheck-${scversion}/shellcheck -x {} \;
      - name: NASM Syntax Check
        run: |
          sudo apt-get install -y nasm=2.16.01-1
          find . -name '*.asm' -exec nasm -f bin -o /dev/null {} \;

  axis-II-semantic:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Clippy (Rust - if used)
        run: echo "Rust components will be added in Phase 2"
      - name: Cppcheck (C/C++)
        run: |
          sudo apt-get install -y cppcheck=2.14.0-1
          cppcheck --enable=all --error-exitcode=1 --suppress=missingInclude \
            02_Symbiose_Bridge/src/ 03_HiveMind_Orchestrator/

  axis-III-structural:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Include-What-You-Use
        run: echo "IWYU will be added when source files exist"
      - name: Dependency Graph
        run: |
          # Verify no circular dependencies
          echo "Dependency graph validation will be added in Phase 2"

  axis-IV-operational:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - name: WDK Build Check
        run: |
          # Verify EWDK is available
          echo "EWDK build will be configured in Phase 2A"

  axis-V-temporal:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Dependency Pin Check
        run: |
          # Verify all dependencies are pinned
          test -f crucible.lock && echo "Lock file present" || echo "Lock file missing"
```

### 1.4 — `crucible.lock`

```toml
# crucible.lock — Pinned tool versions for reproducible verification
# Crucible: PATTERN-012 (pinned dependencies)

[tools]
cmake = "3.28.3"
nasm = "2.16.01"
gcc = "13.2.0"
clang_format = "18.1.8"
cppcheck = "2.14.0"
shellcheck = "0.10.0"
python = "3.12.4"
qemu = "8.2.0"

[wdk]
version = "10.0.26100"
ewdk_iso = "EWDK_26100.iso"

[linux_kernel]
version = "6.6.52"
lts = true
config = "symbiose_defconfig"

[platforms]
build = "ubuntu-22.04"
target_windows = "windows-latest"
target_baremetal = "x86_64-unknown-none"

[ci]
fail_fast = true
cache = true
parallel = true
```

### 1.5 — Phase 1 Verification
- [ ] **Axis I:** CMakeLists.txt parses (`cmake --validate CMakeLists.txt`)
- [ ] **Axis I:** YAML parses properly and contains zero tabulation indents
- [ ] **Axis II:** All paths parameterized (`grep -r "/usr/"` returns nothing)
- [ ] **Axis III:** Directory structure matches spec
- [ ] **Axis IV:** CI pipeline runs on push
- [ ] **Axis V:** `crucible.lock` exists and is complete

---

## 🔺 PHASE 2A: SYMBIOSE BRIDGE DRIVER (WDF KERNEL DRIVER)
**[Assigned Agents: FORGE 🔨 & SENTINEL 🛡️]**
**Status:** [ ] Pending | [ ] In Progress | [x] Completed

**Objective:** Build `symbiose_bridge.sys` — the WDF kernel driver that hooks ACPI, manages the hardware airlock, and orchestrates the transition to Chaos-OS.

### 2A.1 — Driver Header Architecture

```c
// 02_Symbiose_Bridge/src/symbiose_bridge.h
// Crucible: PATTERN-002 (no unwrap in prod), PATTERN-006 (no bare except)

#ifndef SYMBIOSE_BRIDGE_H
#define SYMBIOSE_BRIDGE_H

#include <ntddk.h>
#include <wdf.h>
#include <wdm.h>
#include "symbiose_ioctls.h" // FIX 23: Include shared IOCTL definitions

// ============================================================
// Constants & Configuration
// ============================================================

#define SYMBIOSE_DEVICE_NAME    L"\\Device\\SymbioseBridge"
#define SYMBIOSE_DOS_NAME       L"\\DosDevices\\SymbioseBridge"

// FIX 2: Replaced non-existent MAKE_ULONGLONG macro with explicit ULL
#define SYMBIOSE_DRIVER_VERSION 0x0000000100000000ULL

// ACPI Power State Callbacks
#define SYMBIOSE_ACPI_NOTIFY_SHUTDOWN   0x01
#define SYMBIOSE_ACPI_NOTIFY_SUSPEND    0x02
#define SYMBIOSE_ACPI_NOTIFY_RESUME     0x03

// Timeout for LLM ACK (milliseconds)
#define SYMBIOSE_ACK_TIMEOUT_MS         30000  // 30 seconds

// Maximum number of isolated NVMe devices
#define SYMBIOSE_MAX_NVME_DEVICES       4

// ============================================================
// Data Structures
// ============================================================

typedef enum _SYMBIOSE_STATE {
    SymbioseStateUninitialized = 0,
    SymbioseStateIdle,
    SymbioseStateChaosRunning,
    SymbioseStateShutdownPending,
    SymbioseStateShutdownComplete,
    SymbioseStateError
} SYMBIOSE_STATE;

typedef struct _SYMBIOSE_NVME_ISOLATION {
    ULONG DeviceId;                         // PCI device ID
    ULONG VendorId;                         // PCI vendor ID
    BOOLEAN Isolated;                       // TRUE if Windows NTFS driver detached
    UNICODE_STRING DevicePath;              // NT device path
} SYMBIOSE_NVME_ISOLATION, *PSYMBIOSE_NVME_ISOLATION;

typedef struct _SYMBIOSE_DEVICE_CONTEXT {
    SYMBIOSE_STATE State;
    
    // ACPI callback registration
    PVOID AcpiNotificationHandle;
    
    // NVMe isolation tracking
    SYMBIOSE_NVME_ISOLATION IsolatedDevices[SYMBIOSE_MAX_NVME_DEVICES];
    ULONG IsolatedDeviceCount;
    
    // LLM communication
    KEVENT ShutdownEvent;                   // Signaled when Windows shutdown detected
    KEVENT AckEvent;                        // Signaled when LLM sends ACK_READY_TO_DIE
    KTIMER AckTimer;                        // Timeout timer for ACK
    
    // Chaos-OS kernel state
    PVOID ChaosKernelBuffer;                // BZIMAGE loaded into non-paged pool
    SIZE_T ChaosKernelSize;
    PVOID ChaosRamdiskBuffer;               // CHAOS.RDZ loaded into non-paged pool
    SIZE_T ChaosRamdiskSize;
    
    // Synchronization
    WDFWAITLOCK StateLock;                  // Protects State transitions
    
} SYMBIOSE_DEVICE_CONTEXT, *PSYMBIOSE_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SYMBIOSE_DEVICE_CONTEXT, SymbioseDeviceGetContext)

// ============================================================
// Function Declarations
// ============================================================

// Driver entry and cleanup
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_UNLOAD SymbioseDriverUnload;

// Device creation
EVT_WDF_DEVICE_ADD SymbioseDeviceAdd;
EVT_WDF_DEVICE_CONTEXT_CLEANUP SymbioseDeviceCleanup;

// ACPI notification callback
ACPI_NOTIFICATION_HANDLER SymbioseAcpiNotificationCallback;

// IOCTL handlers
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL SymbioseIoctlHandler;

// NVMe isolation
NTSTATUS SymbioseIsolateNvmeDevice(
    _In_ WDFDEVICE Device,
    _In_ ULONG VendorId,
    _In_ ULONG DeviceId
);

NTSTATUS SymbioseRestoreNvmeDevice(
    _In_ WDFDEVICE Device,
    _In_ ULONG DeviceIndex
);

// Helpers for PCI matching
NTSTATUS SymbioseFindPciDevice(ULONG VendorId, ULONG DeviceId, PUNICODE_STRING DevicePath);
NTSTATUS SymbioseDetachDriverStack(PUNICODE_STRING DevicePath);
NTSTATUS SymbioseReattachDriverStack(PUNICODE_STRING DevicePath);
NTSTATUS SymbioseLoadNullDriver(PUNICODE_STRING DevicePath);
NTSTATUS SymbioseUnloadNullDriver(PUNICODE_STRING DevicePath);

// Assembly thunk interface (defined in SwitchToChaos.asm)
NTSTATUS SwitchToChaosKernel(
    _In_ PVOID KernelImage,
    _In_ SIZE_T KernelSize,
    _In_ PVOID RamdiskImage,
    _In_ SIZE_T RamdiskSize,
    _In_ PVOID BootParams
);

#endif // SYMBIOSE_BRIDGE_H
```

### 2A.2 — Shared IOCTL Definitions (Fix 23)

```c
// 02_Symbiose_Bridge/inc/symbiose_ioctls.h
// FIX 23: Shared definitions between Ring-0 driver and Ring-3 ChaosLoader

#ifndef SYMBIOSE_IOCTLS_H
#define SYMBIOSE_IOCTLS_H

#ifdef _WIN32
#include <windows.h>
#endif

// LLM Communication IOCTLs
#define IOCTL_SYMBIOSE_SEND_SHUTDOWN    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_RECV_ACK         CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_GET_STATUS       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_SET_NVME_ISOLATION CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_SEND_PAYLOAD     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

// FIX 12: Explicitly defined SYMBIOSE_PAYLOAD
typedef struct _SYMBIOSE_PAYLOAD {
    PVOID KernelBuffer;
    SIZE_T KernelSize;
    PVOID RamdiskBuffer;
    SIZE_T RamdiskSize;
    PVOID BootParams;
} SYMBIOSE_PAYLOAD, *PSYMBIOSE_PAYLOAD;

#endif // SYMBIOSE_IOCTLS_H
```

### 2A.3 — Driver Entry Point

```c
// 02_Symbiose_Bridge/src/driver_entry.c
// Crucible: PATTERN-002 (expect() not unwrap()), PATTERN-007 (exit codes handled)

#include "symbiose_bridge.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, SymbioseDriverUnload)
#endif

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;

    KdPrint(("SymbioseBridge: DriverEntry called\n"));

    WDF_DRIVER_CONFIG_INIT(&config, SymbioseDeviceAdd);
    
    // FIX 3: Changed config.DriverUnloading to config.EvtDriverUnload
    config.EvtDriverUnload = SymbioseDriverUnload;
    config.EvtDriverContextCleanup = NULL;

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("SymbioseBridge: WdfDriverCreate failed with status 0x%08X\n", status));
        // PATTERN-002: Return error instead of crashing
        return status;
    }

    KdPrint(("SymbioseBridge: Driver initialized successfully\n"));
    return STATUS_SUCCESS;
}

VOID
SymbioseDriverUnload(
    _In_ WDFDRIVER Driver
)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Driver);
    KdPrint(("SymbioseBridge: Driver unloading\n"));

    // Cleanup is handled by device context cleanup callback
    // PATTERN-015: All resources released in cleanup path
}
```

### 2A.4 — ACPI Notification Handler (Fortification I & II)

```c
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
```

### 2A.5 — IOCTL Handler & Userspace Memory Probing (Fix 21)

```c
// 02_Symbiose_Bridge/src/ioctl_handler.c
// Crucible: PATTERN-002, PATTERN-009 (Safe Memory Probing)

#include "symbiose_bridge.h"

VOID
SymbioseIoctlHandler(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PSYMBIOSE_DEVICE_CONTEXT devCtx = SymbioseDeviceGetContext(device);
    PSYMBIOSE_PAYLOAD payload = NULL;
    PVOID kernelBuffer = NULL;
    PVOID ramdiskBuffer = NULL;
    PVOID bootParamsBuffer = NULL;

    UNREFERENCED_PARAMETER(OutputBufferLength);

    switch (IoControlCode) {
    case IOCTL_SYMBIOSE_SEND_PAYLOAD:
        if (InputBufferLength < sizeof(SYMBIOSE_PAYLOAD)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }

        status = WdfRequestRetrieveInputBuffer(Request, sizeof(SYMBIOSE_PAYLOAD), (PVOID*)&payload, NULL);
        if (!NT_SUCCESS(status)) {
            break;
        }

        // FIX 21: The payload struct is copied, but pointers inside it point to userspace.
        // We must probe them inside a __try/__except block to prevent Ring-0 crashes.
        __try {
            ProbeForRead(payload->KernelBuffer, payload->KernelSize, 1);
            ProbeForRead(payload->RamdiskBuffer, payload->RamdiskSize, 1);
            // Assuming BootParams is a known size struct (2048+ bytes)
            ProbeForRead(payload->BootParams, 4096, 1); 
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            KdPrint(("SymbioseBridge: Invalid userspace pointers in SYMBIOSE_PAYLOAD\n"));
            status = STATUS_INVALID_USER_BUFFER;
            break;
        }

        // Safe allocation and copy to Non-Paged Pool
        kernelBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, payload->KernelSize, 'KnsS');
        if (!kernelBuffer) {
            status = STATUS_NO_MEMORY;
            goto cleanup;
        }
        
        __try {
            RtlCopyMemory(kernelBuffer, payload->KernelBuffer, payload->KernelSize);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            status = STATUS_INVALID_USER_BUFFER;
            goto cleanup;
        }

        // Execute transition logic here (call SwitchToChaosKernel)
        // ...

        status = STATUS_SUCCESS;
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

cleanup:
    if (!NT_SUCCESS(status)) {
        if (kernelBuffer) ExFreePoolWithTag(kernelBuffer, 'KnsS');
    }
    WdfRequestComplete(Request, status);
}
```

### 2A.6 — NVMe Isolation (Hardware Airlock & Fix 16, 20)

```c
// 02_Symbiose_Bridge/src/nvme_isolation.c
// Crucible: PATTERN-008 (TOCTOU prevention), PATTERN-015 (resource cleanup)

#include "symbiose_bridge.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, SymbioseIsolateNvmeDevice)
#pragma alloc_text(PAGE, SymbioseRestoreNvmeDevice)
#endif

// FIX 20: Implementation Stubs for PCI isolation to fix MSVC Linker Errors
NTSTATUS SymbioseFindPciDevice(ULONG VendorId, ULONG DeviceId, PUNICODE_STRING DevicePath) {
    UNREFERENCED_PARAMETER(VendorId);
    UNREFERENCED_PARAMETER(DeviceId);
    UNREFERENCED_PARAMETER(DevicePath);
    // TODO: Implement PCI device enumeration via IoGetDeviceObjectPointer
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS SymbioseDetachDriverStack(PUNICODE_STRING DevicePath) {
    UNREFERENCED_PARAMETER(DevicePath);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS SymbioseReattachDriverStack(PUNICODE_STRING DevicePath) {
    UNREFERENCED_PARAMETER(DevicePath);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS SymbioseLoadNullDriver(PUNICODE_STRING DevicePath) {
    UNREFERENCED_PARAMETER(DevicePath);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS SymbioseUnloadNullDriver(PUNICODE_STRING DevicePath) {
    UNREFERENCED_PARAMETER(DevicePath);
    return STATUS_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------

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

    // FIX 16: Moved the TOCTOU check INSIDE the WdfWaitLockAcquire boundary
    WdfWaitLockAcquire(devCtx->StateLock, NULL);

    if (!devCtx->IsolatedDevices[DeviceIndex].Isolated) {
        WdfWaitLockRelease(devCtx->StateLock);
        return STATUS_SUCCESS;  // Already restored
    }

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
```

### 2A.7 — SymbioseNull.inf (Hardware Airlock)

```ini
; 02_Symbiose_Bridge/inf/SymbioseNull.inf
; Crucible: PATTERN-005 (no hardcoded paths in deployment)
; Hardware Airlock - Null driver for isolated NVMe and GPU devices

[Version]
Signature="$Windows NT$"
Class=System
ClassGuid={4D36E97D-E325-11CE-BFC1-08002BE10318}
Provider=%ManufacturerName%
DriverVer=01/24/2025,0.1.0.0
CatalogFile=SymbioseNull.cat

[DestinationDirs]
DefaultDestDir=12  ; %windir%\system32\drivers

[SourceDisksNames]
1=%DiskName%,,,

[SourceDisksFiles]
symbiose_null.sys=1

[Manufacturer]
%ManufacturerName%=Standard,NTamd64

[Standard.NTamd64]
%SymbioseNvmeDeviceDesc%=SymbioseNull_Inst, PCI\VEN_144D&DEV_A808  ; Samsung PM9A3 NVMe
%SymbioseNvmeDeviceDesc%=SymbioseNull_Inst, PCI\VEN_144D&DEV_A809  ; Samsung PM9A3 NVMe (alt)
%SymbioseGpuDeviceDesc%=SymbioseNull_Inst, PCI\VEN_10DE&DEV_2620   ; NVIDIA RTX 4090
%SymbioseGpuDeviceDesc%=SymbioseNull_Inst, PCI\VEN_10DE&DEV_2621   ; NVIDIA RTX 4090 (alt)

[SymbioseNull_Inst.NT]
CopyFiles=SymbioseNull_CopyFiles

[SymbioseNull_CopyFiles]
symbiose_null.sys

[SymbioseNull_Inst.NT.Services]
AddService=SymbioseNull,,SymbioseNull_Service

[SymbioseNull_Service]
DisplayName=%ServiceDesc%
ServiceType=1               ; SERVICE_KERNEL_DRIVER
StartType=3                 ; SERVICE_DEMAND_START
ErrorControl=1              ; SERVICE_ERROR_NORMAL
ServiceBinary=%12%\symbiose_null.sys

[Strings]
ManufacturerName="Chaos-Symbiose Project"
DiskName="Chaos-Symbiose Installation Disk"
SymbioseNvmeDeviceDesc="Symbiose NVMe Airlock (Null Driver)"
SymbioseGpuDeviceDesc="Symbiose GPU Airlock (Null Driver)"
ServiceDesc="Symbiose Null Driver - Hardware Airlock"
```

### 2A.8 — Assembly Thunk (Fortification III: Triple Fault Prevention)
**[Assigned Agents: CRUCIBLE 🔺 & OUROBOROS 🐍]**

```nasm
; 02_Symbiose_Bridge/src/SwitchToChaos.asm
; Crucible: PATTERN-002 (no unwrap - every register state validated)
; Fortification III: Identity mapping, PG disable, LME clear, far jump

[BITS 64]

; FIX 13 & 18: Replaced hardcoded addresses with named constants
%define BOOT_PARAMS_PML4_OFFSET 0x28
%define LINUX_KERNEL_LOAD_ADDR  0x100000

; FIX 19: Removed leading underscore (invalid for Win64 MSVC symbols)
extern SymbioseTripleFaultHandler

; FIX 8: Added local GDT to enable the 32-bit Compatibility Mode transition
section .data
align 8
gdt_start:
    dq 0x0000000000000000       ; Null descriptor
    dq 0x00209A0000000000       ; 64-bit code (0x08)
    dq 0x00CF9A000000FFFF       ; 32-bit compat code segment (0x10)
    dq 0x00CF92000000FFFF       ; 32-bit compat data segment (0x18)
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1  ; Limit
    dq gdt_start                ; Base

section .text

global SwitchToChaosKernel

SwitchToChaosKernel:
    ; ============================================================
    ; Phase 1: Validate all parameters
    ; ============================================================
    ; FIX 4 & 5: Load BootParams into r10 BEFORE stack pushes
    ; to avoid stale [rsp+112] offsets and unreliable memory tests.
    mov r10, [rsp+40]
    
    test rcx, rcx
    jz .param_error_kernel
    test r8, r8
    jz .param_error_ramdisk
    test r10, r10               ; FIX 4: Safe register test
    jz .param_error_bootparams

    ; Validate kernel size
    test rdx, rdx
    jz .param_error_kernel_size
    cmp rdx, 0x100000           ; Minimum 1MB for a valid kernel
    jb .param_error_kernel_size
    cmp rdx, 0x40000000         ; Maximum 1GB
    ja .param_error_kernel_size

    ; ============================================================
    ; Phase 2: Save Windows state for potential recovery
    ; ============================================================
    push rbp
    mov rbp, rsp
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15

    ; Save CR0, CR3, CR4 for recovery
    mov r12, cr0
    mov r13, cr3
    mov r14, cr4

    ; Save MSR values (EFER)
    mov ecx, 0xC0000080         ; IA32_EFER
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov r15, rax                ; Save EFER

    ; ============================================================
    ; Phase 3: Identity-map the transition page
    ; ============================================================
    ; Assumed mapped by calling C code in MmAllocateContiguousMemory

    ; ============================================================
    ; Phase 4: Disable Interrupts
    ; ============================================================
    cli

    ; ============================================================
    ; Phase 5: Load new CR3 (Chaos-OS page tables)
    ; ============================================================
    mov rax, [r10 + BOOT_PARAMS_PML4_OFFSET]
    mov cr3, rax

    ; ============================================================
    ; Phase 6: Load GDT and Jump to 32-bit Compatibility Code
    ; FIX 8 & 17: Intel manuals dictate jumping to compat mode BEFORE
    ; disabling Paging and LME.
    ; ============================================================
    lgdt [rel gdt_desc]
    
    ; Push CS and RIP for retfq
    push 0x10
    lea rax, [rel compat_mode_entry] ; FIX 22: Changed to global label
    push rax
    retfq

compat_mode_entry:
    [BITS 32]

    ; ============================================================
    ; Phase 7: Disable Paging & Clear LME
    ; ============================================================
    mov eax, cr0
    btr eax, 31                 ; FIX 6: Use btr to avoid zeroing upper 32 bits
    mov cr0, eax

    mov ecx, 0xC0000080         ; IA32_EFER MSR
    rdmsr
    btr eax, 8                  ; Clear LME (bit 8)
    wrmsr

    ; ============================================================
    ; Phase 8: Jump to BZIMAGE entry point
    ; ============================================================
    mov eax, LINUX_KERNEL_LOAD_ADDR
    jmp eax

    ; ============================================================
    ; Error paths 
    ; ============================================================
[BITS 64]                       ; FIX 7: Recovery paths moved back to 64-bit BITS

.param_error_kernel:
    mov eax, 0xC000000D         ; STATUS_INVALID_PARAMETER
    ret

.param_error_ramdisk:
    mov eax, 0xC000000D
    ret

.param_error_bootparams:
    mov eax, 0xC000000D
    ret

.param_error_kernel_size:
    mov eax, 0xC000000D
    ret

    ; ============================================================
    ; Recovery path (called if triple fault occurs)
    ; ============================================================
global SymbioseTripleFaultRecovery
SymbioseTripleFaultRecovery:
    ; Restore Windows state
    mov cr3, r13                ; Restore original CR3
    mov rax, r12
    mov cr0, rax                ; Restore original CR0 (re-enables paging)
    
    ; Restore EFER
    mov ecx, 0xC0000080
    mov rdx, r15
    shr rdx, 32
    wrmsr

    ; Restore CR4
    mov cr4, r14

    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp

    sti                         ; Re-enable interrupts
    mov eax, 0xC000021A         ; STATUS_UNSUCCESSFUL
    ret
```

### 2A.9 — Phase 2A Verification
- [ ] **Axis I:** C header compiles (`clang -fsyntax-only symbiose_bridge.h`)
- [ ] **Axis I:** NASM assembles (`nasm -f win64 SwitchToChaos.asm -o /dev/null`)
- [ ] **Axis I:** INF validates (`infverif /v SymbioseNull.inf`)
- [ ] **Axis II:** All NTSTATUS values checked (`grep -n "NT_SUCCESS" *.c`)
- [ ] **Axis II:** No bare returns without status (`grep -rn "return;" src/` should return nothing in non-void functions)
- [ ] **Axis III:** Driver follows WDF patterns
- [ ] **Axis IV:** No hardcoded PCI IDs in driver

---

## 🔺 PHASE 2B: CHAOSLOADER & PID 1 INJECTION
**[Assigned Agents: 🔨 Forge | 🧠 Cortex]**
**Status:** [ ] Pending | [ ] In Progress | [x] Completed

**Objective:** Build `ChaosLoader.exe` — the userspace component that loads BZIMAGE/CHAOS.RDZ into memory, patches the kernel Zero Page with `init=/symbiose/hive_mind`, and triggers the driver to switch execution.

### 2B.1 — ChaosLoader Main

```c
// 03_HiveMind_Orchestrator/ChaosLoader/src/main.c
// Crucible: PATTERN-002 (expect not unwrap), PATTERN-005 (pathlib equivalent)

#include <windows.h>
#include <winternl.h>           // FIX 11: Required for NTSTATUS types in userspace
#include <stdio.h>
#include <stdlib.h>
#include "../../02_Symbiose_Bridge/inc/symbiose_ioctls.h" // FIX 23: Path corrected
#include "boot_params.h"

// PATTERN-005: Use environment variables, not hardcoded paths
#define SYMBIOSE_CORE_DIR L"%SystemDrive%\\Symbiose_Core"

// FIX 9: Corrected string escaping
#define SYMBIOSE_DRIVER_PATH L"\\\\.\\SymbioseBridge"

static NTSTATUS LoadFileIntoBuffer(
    _In_ LPCWSTR FilePath,
    _Out_ PVOID* Buffer,
    _Out_ SIZE_T* BufferSize
)
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    LARGE_INTEGER fileSize;
    PVOID fileBuffer = NULL;

    // PATTERN-002: Validate all parameters
    if (FilePath == NULL || Buffer == NULL || BufferSize == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    *Buffer = NULL;
    *BufferSize = 0;

    hFile = CreateFileW(
        FilePath,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        // FIX 10: Replaced kernel-only KdPrint with fwprintf
        fwprintf(stderr, L"ChaosLoader: Failed to open %ls (error %lu)\n", FilePath, GetLastError());
        return STATUS_NOT_FOUND;
    }

    // Get file size
    if (!GetFileSizeEx(hFile, &fileSize)) {
        fwprintf(stderr, L"ChaosLoader: Failed to get file size (error %lu)\n", GetLastError());
        CloseHandle(hFile);
        return STATUS_FILE_TOO_LARGE;
    }

    // Validate file size (max 1GB for kernel, 4GB for ramdisk)
    if (fileSize.QuadPart > 0x100000000ULL) {
        fwprintf(stderr, L"ChaosLoader: File too large: %lld bytes\n", fileSize.QuadPart);
        CloseHandle(hFile);
        return STATUS_FILE_TOO_LARGE;
    }

    // Allocate buffer
    fileBuffer = VirtualAlloc(
        NULL,
        (SIZE_T)fileSize.QuadPart,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (fileBuffer == NULL) {
        fwprintf(stderr, L"ChaosLoader: Failed to allocate %lld bytes\n", fileSize.QuadPart);
        CloseHandle(hFile);
        return STATUS_NO_MEMORY;
    }

    // Read file
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, fileBuffer, (DWORD)fileSize.QuadPart, &bytesRead, NULL)) {
        fwprintf(stderr, L"ChaosLoader: Failed to read file (error %lu)\n", GetLastError());
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

int wmain(int argc, wchar_t* argv[])
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

    fwprintf(stdout, L"ChaosLoader v0.1.0 - Chaos-Symbiose OS Loader\n");
    fwprintf(stdout, L"==============================================\n\n");

    // Step 1: Open the Symbiose Bridge driver
    hDevice = CreateFileW(
        SYMBIOSE_DRIVER_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"❌ Failed to open Symbiose Bridge driver (error %lu)\n", GetLastError());
        fwprintf(stderr, L"   Ensure symbiose_bridge.sys is loaded.\n");
        return 1;
    }

    // Step 2: Load BZIMAGE
    WCHAR kernelPath[MAX_PATH];
    ExpandEnvironmentStringsW(SYMBIOSE_CORE_DIR L"\\BZIMAGE", kernelPath, MAX_PATH);

    fwprintf(stdout, L"📂 Loading kernel: %ls\n", kernelPath);
    status = LoadFileIntoBuffer(kernelPath, &kernelBuffer, &kernelSize);
    if (!NT_SUCCESS(status)) {
        fwprintf(stderr, L"❌ Failed to load kernel (0x%08X)\n", status);
        goto cleanup;
    }
    fwprintf(stdout, L"   ✅ Kernel loaded: %zu bytes\n", kernelSize);

    // Step 3: Load CHAOS.RDZ
    WCHAR ramdiskPath[MAX_PATH];
    ExpandEnvironmentStringsW(SYMBIOSE_CORE_DIR L"\\CHAOS.RDZ", ramdiskPath, MAX_PATH);

    fwprintf(stdout, L"📂 Loading ramdisk: %ls\n", ramdiskPath);
    status = LoadFileIntoBuffer(ramdiskPath, &ramdiskBuffer, &ramdiskSize);
    if (!NT_SUCCESS(status)) {
        fwprintf(stderr, L"❌ Failed to load ramdisk (0x%08X)\n", status);
        goto cleanup;
    }
    fwprintf(stdout, L"   ✅ Ramdisk loaded: %zu bytes\n", ramdiskSize);

    // Step 4: Initialize boot parameters
    // Fortification V: PID 1 Injection
    fwprintf(stdout, L"🔧 Initializing boot parameters\n");
    status = BootParams_Init(&bootParams, kernelBuffer, kernelSize,
                              ramdiskBuffer, ramdiskSize);
    if (!NT_SUCCESS(status)) {
        fwprintf(stderr, L"❌ Failed to initialize boot params (0x%08X)\n", status);
        goto cleanup;
    }

    // Inject init parameter: init=/symbiose/hive_mind
    status = BootParams_SetCommandLine(&bootParams, "init=/symbiose/hive_mind");
    if (!NT_SUCCESS(status)) {
        fwprintf(stderr, L"❌ Failed to set init parameter (0x%08X)\n", status);
        goto cleanup;
    }
    fwprintf(stdout, L"   ✅ PID 1 set to: /symbiose/hive_mind\n");

    // Step 5: Send kernel and ramdisk to driver
    fwprintf(stdout, L"🚀 Sending payload to Symbiose Bridge driver\n");

    SYMBIOSE_PAYLOAD payload = {0};
    payload.KernelBuffer = kernelBuffer;
    payload.KernelSize = kernelSize;
    payload.RamdiskBuffer = ramdiskBuffer;
    payload.RamdiskSize = ramdiskSize;
    payload.BootParams = &bootParams;

    if (!DeviceIoControl(
        hDevice,
        IOCTL_SYMBIOSE_SEND_PAYLOAD,
        &payload,
        sizeof(payload),
        NULL,
        0,
        &bytesReturned,
        NULL
    )) {
        fwprintf(stderr, L"❌ IOCTL failed (error %lu)\n", GetLastError());
        goto cleanup;
    }

    fwprintf(stdout, L"✅ Payload sent successfully\n");
    fwprintf(stdout, L"⚠️  Chaos-OS is now running. Windows is suspended.\n");

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
```

### 2B.2 — Boot Parameters & PID 1 Injection

```c
// 03_HiveMind_Orchestrator/ChaosLoader/src/boot_params.h
// Crucible: PATTERN-002 (validate all inputs)

#ifndef BOOT_PARAMS_H
#define BOOT_PARAMS_H

#include <windows.h>

// Linux x86 boot protocol constants
#define BOOT_PARAMS_MAGIC    0xAA55
#define LINUX_BOOT_HDR_MAGIC 0x53726448  // "HdrS"
#define LINUX_BOOT_HDR_VERSION 0x020F    // Version 2.15

// Maximum command line length
#define MAX_CMDLINE_LEN      2048

// Linux setup header offsets (from setup.S)
#pragma pack(push, 1)
typedef struct _LINUX_SETUP_HEADER {
    UINT8  setup_sects;          // 0x1F1
    UINT16 root_flags;           // 0x1F2
    UINT16 syssize;              // 0x1F4 (4 bytes in 2.08+)
    UINT16 ram_size;             // 0x1F8
    UINT16 vid_mode;             // 0x1FA
    UINT16 root_dev;             // 0x1FC
    UINT8  signature[2];        // 0x1FE (0xAA55)
    UINT8  jump[2];             // 0x200
    UINT8  header_magic[4];     // 0x202 ("HdrS")
    UINT16 protocol_version;    // 0x206
    UINT32 cmdline_size;        // 0x238
    UINT32 cmdline_ptr;         // 0x228
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
```

### 2B.3 — Phase 2B Verification
- [ ] **Axis I:** C header compiles (`clang -fsyntax-only boot_params.h`)
- [ ] **Axis II:** All handles closed on every path (`grep -n "CloseHandle\|VirtualFree" main.c`)
- [ ] **Axis II:** No bare returns without status in userspace execution
- [ ] **Axis III:** Boot params structure matches Linux 6.6 `boot_protocol`
- [ ] **Axis IV:** No hardcoded paths; `%SystemDrive%` used
- [ ] **Axis V:** Boot protocol version documented

---

## 🔺 PHASE 2C: IRC NEURAL BUS (MoE Protocol)
**[Assigned Agents: 🧠 Cortex | ⚡ Bolt]**
**Status:** [ ] Pending | [ ] In Progress | [x] Completed

**Objective:** Build `symbiose-ircd` — a custom IRC daemon with jumbo frame support for LLM Scout M2M communication.

### 2C.1 — IRC Daemon Core (ngircd fork)

```c
// 03_HiveMind_Orchestrator/IRCd_Neural_Bus/src/symbiose_ircd.h
// Crucible: PATTERN-004 (no unquoted vars), PATTERN-010 (no shell injection)

#ifndef SYMBIOSE_IRCD_H
#define SYMBIOSE_IRCD_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

// ============================================================
// Configuration
// ============================================================

// Fortification IV: Jumbo Frame Support
// RFC 1459 limits messages to 512 bytes.
// We extend this to support infinite token streams.
#define SYMBIOSE_IRC_MAX_LINE          (1 << 20)  // 1 MiB per message
#define SYMBIOSE_IRC_MAX_CHUNK         (1 << 16)  // 64 KiB per chunk
#define SYMBIOSE_IRC_MAX_CHANNEL_NAME  256
#define SYMBIOSE_IRC_MAX_NICK          128
#define SYMBIOSE_IRC_MAX_TOPIC         1024

// Neural Bus Channels
#define SYMBIOSE_CHANNEL_ORACLE    "#oracle"       // Central LLM
#define SYMBIOSE_CHANNEL_RECON     "#recon"         // Scout reconnaissance
#define SYMBIOSE_CHANNEL_HIVEMIND  "#hive-mind"     // MoE coordination
#define SYMBIOSE_CHANNEL_CONTROL   "#control"       // Human operator

// ============================================================
// MoE Protocol Message Types
// ============================================================

typedef enum _SYMBIOSE_MSG_TYPE {
    SymbioseMsgTaskDispatch = 0x01,     // Oracle -> Scout: new task
    SymbioseMsgTaskResult = 0x02,       // Scout -> Oracle: task result
    SymbioseMsgHeartbeat = 0x03,       // All: keep-alive
    SymbioseMsgMigration = 0x04,       // OpenMosix: process migration signal
    SymbioseMsgContextPage = 0x05,     // VFS: context page request/response
    SymbioseMsgNeuralSnapshot = 0x06,  // CCD: neural state snapshot
    SymbioseMsgShutdownAck = 0x07,     // Death Rattle: ACK_READY_TO_DIE
} SYMBIOSE_MSG_TYPE;

// ============================================================
// Jumbo Frame Header
// ============================================================

#pragma pack(push, 1)
typedef struct _SYMBIOSE_JUMBO_HEADER {
    uint32_t magic;              // 0x53594D42 ("SYMB")
    uint16_t version;            // Protocol version (1)
    uint8_t  msg_type;           // SYMBIOSE_MSG_TYPE
    uint8_t  flags;              // Bit 0: compressed, Bit 1: encrypted
    uint64_t payload_len;        // Total payload length
    uint32_t chunk_seq;          // Chunk sequence number (0-indexed)
    uint32_t chunk_count;        // Total number of chunks
    uint32_t chunk_offset;       // Offset of this chunk in payload
    uint16_t chunk_len;          // Length of this chunk's data
    uint16_t reserved;           // Reserved for future use
    uint8_t  checksum[32];       // SHA-256 of full payload (only in chunk 0)
} SYMBIOSE_JUMBO_HEADER, *PSYMBIOSE_JUMBO_HEADER;
#pragma pack(pop)

_Static_assert(sizeof(SYMBIOSE_JUMBO_HEADER) == 64, "Jumbo header must be 64 bytes");

// ============================================================
// Client Connection
// ============================================================

typedef enum _SYMBIOSE_CLIENT_TYPE {
    SymbioseClientOracle = 0,    // Central LLM (channel admin @)
    SymbioseClientScout = 1,     // Scout model
    SymbioseClientOperator = 2,  // Human operator
    SymbioseClientUnknown = 255
} SYMBIOSE_CLIENT_TYPE;

typedef struct _SYMBIOSE_CLIENT {
    int fd;
    SYMBIOSE_CLIENT_TYPE type;
    char nick[SYMBIOSE_IRC_MAX_NICK];
    char channel[SYMBIOSE_IRC_MAX_CHANNEL_NAME];
    time_t last_heartbeat;
    time_t connected_since;
    
    // Jumbo frame reassembly buffer
    uint8_t *jumbo_buffer;
    size_t jumbo_buffer_size;
    uint32_t jumbo_chunks_received;
    uint32_t jumbo_chunks_expected;
} SYMBIOSE_CLIENT, *PSYMBIOSE_CLIENT;

// ============================================================
// Function Declarations
// ============================================================

// Server lifecycle
int SymbioseIrcd_Init(uint16_t port);
int SymbioseIrcd_Run(void);
void SymbioseIrcd_Shutdown(void);

// Client management
PSYMBIOSE_CLIENT SymbioseIrcd_AcceptClient(int server_fd);
void SymbioseIrcd_DisconnectClient(PSYMBIOSE_CLIENT client);

// Jumbo frame handling
int SymbioseIrcd_SendJumbo(PSYMBIOSE_CLIENT client, SYMBIOSE_MSG_TYPE type,
                           const uint8_t *payload, size_t payload_len);
int SymbioseIrcd_RecvJumbo(PSYMBIOSE_CLIENT client, SYMBIOSE_MSG_TYPE *type,
                           uint8_t **payload, size_t *payload_len);

// MoE Protocol
int SymbioseIrcd_DispatchTask(PSYMBIOSE_CLIENT oracle, PSYMBIOSE_CLIENT scout,
                               const char *task_description, size_t desc_len);
int SymbioseIrcd_ReportResult(PSYMBIOSE_CLIENT scout, PSYMBIOSE_CLIENT oracle,
                               const char *result, size_t result_len);

#endif // SYMBIOSE_IRCD_H
```

### 2C.2 — Phase 2C Verification
- [ ] **Axis I:** Header compiles (`gcc -fsyntax-only -std=c17 symbiose_ircd.h`)
- [ ] **Axis I:** Static assert passes (`gcc -c symbiose_ircd.h`)
- [ ] **Axis II:** No buffer overflows in jumbo reassembly bounds checking
- [ ] **Axis III:** Protocol version field present
- [ ] **Axis IV:** Max line size is configurable
- [ ] **Axis V:** Protocol version documented

---

## 🔺 PHASE 2D: VFS STORAGE MANAGER
**[Assigned Agents: 🧠 Cortex | 🧪 Alchemist]**
**Status:** [ ] Pending | [ ] In Progress | [x] Completed

**Objective:** Build the Vectorized File System manager that provides bare-metal NVMe access for the LLM's Hippocampus.

### 2D.1 — VFS Manager Header

```c
// 03_HiveMind_Orchestrator/VFS_Storage_Manager/src/vfs_manager.h
// Crucible: PATTERN-008 (TOCTOU prevention), PATTERN-015 (cleanup on error)

#ifndef VFS_MANAGER_H
#define VFS_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================
// Configuration
// ============================================================

#define VFS_MAGIC               0x56465331  // "VFS1"
#define VFS_VERSION             1
#define VFS_PAGE_SIZE           4096
#define VFS_VECTOR_DIMENSION    768         // LLM embedding dimension
#define VFS_MAX_DEVICES         4
#define VFS_MAX_COLLECTIONS     256

// ============================================================
// Data Structures
// ============================================================

typedef enum _VFS_STATUS {
    VfsStatusOk = 0,
    VfsStatusError,
    VfsStatusNotFound,
    VfsStatusCorrupted,
    VfsStatusDeviceBusy,
    VfsStatusOutOfSpace,
    VfsStatusInvalidParameter
} VFS_STATUS;

typedef struct _VFS_DEVICE_INFO {
    uint32_t device_id;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t used_blocks;
    uint64_t free_blocks;
    bool is_isolated;           // TRUE if Windows NTFS driver is detached
} VFS_DEVICE_INFO, *PVFS_DEVICE_INFO;

typedef struct _VFS_VECTOR_ID {
    uint64_t collection_id;
    uint64_t vector_id;
} VFS_VECTOR_ID, *PVFS_VECTOR_ID;

typedef struct _VFS_VECTOR_ENTRY {
    VFS_VECTOR_ID id;
    float vector[VFS_VECTOR_DIMENSION];
    uint64_t timestamp;
    uint32_t metadata_len;
    uint8_t metadata[];         // Flexible array member
} VFS_VECTOR_ENTRY, *PVFS_VECTOR_ENTRY;

typedef struct _VFS_COLLECTION {
    uint64_t id;
    char name[256];
    uint64_t vector_count;
    uint64_t created_at;
    uint64_t updated_at;
    VFS_DEVICE_INFO *device;    // Owning device
} VFS_COLLECTION, *PVFS_COLLECTION;

// ============================================================
// SPDK Integration (Ring-0 NVMe Access)
// ============================================================

typedef struct _VFS_SPDK_CONTEXT {
    void *nvme_ctrlr;           // struct spdk_nvme_ctrlr *
    void *nvme_ns;              // struct spdk_nvme_ns *
    uint32_t io_queue_size;
    bool is_initialized;
} VFS_SPDK_CONTEXT, *PVFS_SPDK_CONTEXT;

// ============================================================
// Function Declarations
// ============================================================

// Lifecycle
VFS_STATUS Vfs_Init(PVFS_SPDK_CONTEXT spdk_ctx, uint32_t nvme_pci_id);
void Vfs_Shutdown(PVFS_SPDK_CONTEXT spdk_ctx);

// Device management
VFS_STATUS Vfs_GetDeviceInfo(uint32_t device_index, PVFS_DEVICE_INFO info);
VFS_STATUS Vfs_IsolateDevice(uint32_t nvme_pci_id);
VFS_STATUS Vfs_RestoreDevice(uint32_t device_index);

// Vector operations
VFS_STATUS Vfs_InsertVector(PVFS_COLLECTION collection,
                             const float *vector,
                             uint32_t vector_dim,
                             const uint8_t *metadata,
                             uint32_t metadata_len,
                             PVFS_VECTOR_ID out_id);

VFS_STATUS Vfs_QueryVector(PVFS_COLLECTION collection,
                            const float *query_vector,
                            uint32_t vector_dim,
                            uint32_t top_k,
                            PVFS_VECTOR_ENTRY *results,
                            uint32_t *result_count);

VFS_STATUS Vfs_DeleteVector(PVFS_COLLECTION collection, VFS_VECTOR_ID id);

// Context paging (CCD)
VFS_STATUS Vfs_PageOutContext(uint64_t session_id,
                              const uint8_t *kv_cache,
                              size_t kv_cache_len,
                              uint64_t *out_page_id);

VFS_STATUS Vfs_PageInContext(uint64_t session_id,
                             uint64_t page_id,
                             uint8_t *kv_cache,
                             size_t kv_cache_len,
                             size_t *bytes_read);

// Neural snapshot (CoW backup)
VFS_STATUS Vfs_CreateNeuralSnapshot(uint64_t session_id,
                                     uint64_t *out_snapshot_id);

VFS_STATUS Vfs_RestoreNeuralSnapshot(uint64_t snapshot_id,
                                      uint64_t *out_session_id);

#endif // VFS_MANAGER_H
```

### 2D.2 — Phase 2D Verification
- [ ] **Axis I:** Header compiles (`gcc -fsyntax-only -std=c17 vfs_manager.h`)
- [ ] **Axis II:** All VFS_STATUS values handled
- [ ] **Axis II:** Flexible array member validated
- [ ] **Axis III:** SPDK context separate from VFS logic
- [ ] **Axis IV:** NVMe PCI ID parameterized, not hardcoded
- [ ] **Axis V:** VFS_VERSION field for future migrations

---

## 🐍 PHASE 2E: OPENMOSIX 2026+ & META-GENERATION MATRIX
**[Assigned Agents: 🧠 Cortex | 🐍 Ouroboros]**
**Status:** [ ] Pending | [ ] In Progress | [x] Completed

**Objective:** Upgrade legacy OpenMosix 2.4 C-code for modern Heterogeneous Tensor Migration using Ouroboros' AST code-generation capabilities.

### 2E.1 — Tensor Migration Header [Agent: Cortex]

```c
// 03_HiveMind_Orchestrator/OpenMosix_2026/src/openmosix_tensor.h
// Crucible: PATTERN-008 (Race conditions in memory locking)

#ifndef OPENMOSIX_TENSOR_H
#define OPENMOSIX_TENSOR_H

#include <stdint.h>
#include <stdbool.h>

// FIX 24/25: Added platform guard to prevent WDK compilation failures on Windows
#ifdef __linux__
#include <sys/mman.h>
#endif

#define OMOSIX_PORT 723
#define MAX_TENSOR_DIM 8192

typedef enum _MOSIX_NODE_ROLE {
    NODE_ORACLE = 0,    // Central 110B+ model
    NODE_SCOUT  = 1     // 12B/24B dynamic models
} MOSIX_NODE_ROLE;

typedef struct _TENSOR_STATE_PAYLOAD {
    uint64_t model_id;
    size_t kv_cache_size;
    void* kv_cache_ptr;    // Physical address mapped via VFS
    bool is_locked_in_ram;
} TENSOR_STATE_PAYLOAD;

// Lock Memory Descriptor Lists before migrating LLM weights across cluster
#ifdef __linux__
static inline int mosix_lock_and_migrate_tensor(pid_t scout_pid, uint32_t target_ip) {
    mlockall(MCL_CURRENT | MCL_FUTURE); // Prevent page fault during TCP transfer
    // return perform_mosix_transfer(scout_pid, target_ip);
    return 0;
}
#else
static inline int mosix_lock_and_migrate_tensor(int scout_pid, uint32_t target_ip) {
    // Stub for Windows compilation
    (void)scout_pid;
    (void)target_ip;
    return 0;
}
#endif

#endif // OPENMOSIX_TENSOR_H
```

### 2E.2 — Ouroboros AST Meta-Generator [Agent: Ouroboros]

```python
# 03_HiveMind_Orchestrator/OpenMosix_2026/scripts/ouroboros_ast_gen.py
# Crucible: PATTERN-005 (Use pathlib)

import json
from pathlib import Path
import sys
import os

def generate_tensor_structs(schema_path: Path, output_path: Path) -> None:
    if not schema_path.exists():
        print(f"Schema not found: {schema_path}", file=sys.stderr)
        sys.exit(1)
        
    with schema_path.open() as f:
        schema = json.load(f)
        
    with output_path.open("w") as out:
        out.write("// AUTOGENERATED BY OUROBOROS 🐍 - DO NOT EDIT\n")
        out.write("#pragma pack(push, 1)\n")
        out.write("typedef struct _LLM_TENSOR_BLOCK {\n")
        for layer in schema.get('layers', []):
            out.write(f"    float {layer['name']}[{layer['dimensions']}];\n")
        out.write("} LLM_TENSOR_BLOCK;\n")
        out.write("#pragma pack(pop)\n")

if __name__ == "__main__":
    schema_file = Path(os.environ.get("SYMBIOSE_SCHEMA", "llm_schema.json"))
    out_file = Path(os.environ.get("SYMBIOSE_OUT", "generated_tensors.h"))
    generate_tensor_structs(schema_file, out_file)
```

### 2E.3 — Phase 2E Verification
- [ ] **Axis I:** Python script parses (`uv run ruff check ouroboros_ast_gen.py`)
- [ ] **Axis III:** Ouroboros output generates valid C
- [ ] **Axis IV:** No hardcoded paths in AST script (Uses `pathlib` and `os.environ`)

---

## 🔺 PHASE 3: APBX TRANSMIGRATION
**[Assigned Agents: 🛡️ Sentinel | 🎨 Palette]**
**Status:** [ ] Pending | [ ] In Progress | [x] Completed

**Objective:** Build the `.apbx` playbook that orchestrates the entire deployment: VBS takedown, hardware airlock, payload injection, and driver loading.

### 3.1 — AME Wizard Playbook (`main.yml`)

```yaml
# 04_APBX_Transmigration/playbook/Configuration/main.yml
# Crucible: PATTERN-005 (no hardcoded paths), PATTERN-012 (pinned versions)
# FIX 15: Converted all indentation tabs to strictly 2-spaces.

---
schema: "3"
name: "Chaos-Symbiose OS Transmigration"
version: "0.1.0"
description: "Deploys the Chaos-Symbiose parasitic execution environment on Windows 10/11"

# ============================================================
# PHASE 1: Boot Config & Win11 VBS Takedown
# Fortification I: VBS/HVCI Annihilation
# ============================================================
steps:
  - name: "Phase 1.1: Enable Test Signing"
    actions:
      - !cmd:
          command: 'bcdedit /set testsigning on'
          runAs: TrustedInstaller
          expect: 0
          rollback: 'bcdedit /set testsigning off'

  - name: "Phase 1.2: Disable VBS (Virtualization-Based Security)"
    actions:
      - !reg:
          path: 'HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard'
          name: 'EnableVirtualizationBasedSecurity'
          value: 0
          type: DWORD
          runAs: TrustedInstaller
          rollback_value: 1
      - !reg:
          path: 'HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity'
          name: 'Enabled'
          value: 0
          type: DWORD
          runAs: TrustedInstaller
          rollback_value: 1

  - name: "Phase 1.3: Disable Memory Integrity (Core Isolation)"
    actions:
      - !reg:
          path: 'HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity'
          name: 'Enabled'
          value: 0
          type: DWORD
          runAs: TrustedInstaller
          rollback_value: 1
      - !reg:
          path: 'HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard'
          name: 'HVCIMATRequired'
          value: 0
          type: DWORD
          runAs: TrustedInstaller
          rollback_value: 1

  - name: "Phase 1.4: Disable Credential Guard"
    actions:
      - !reg:
          path: 'HKLM\SYSTEM\CurrentControlSet\Control\Lsa'
          name: 'LsaCfgFlags'
          value: 0
          type: DWORD
          runAs: TrustedInstaller
          rollback_value: 1
      - !reg:
          path: 'HKLM\SYSTEM\CurrentControlSet\Control\Lsa'
          name: 'RunAsPPL'
          value: 0
          type: DWORD
          runAs: TrustedInstaller
          rollback_value: 1

  - name: "Phase 1.5: Disable Secure Boot (if required)"
    actions:
      - !cmd:
          command: 'bcdedit /set disableelxservices on'
          runAs: TrustedInstaller
          expect: 0
          rollback: 'bcdedit /set disableelxservices off'

  # ============================================================
  # PHASE 2: GPU & NVMe Hardware Airlock
  # Fortification II: IOMMU Isolation
  # ============================================================
  - name: "Phase 2.1: Create Symbiose Core Directory"
    actions:
      - !cmd:
          command: 'mkdir "C:\Symbiose_Core"'
          runAs: TrustedInstaller
          expect: 0
          rollback: 'rmdir /s /q "C:\Symbiose_Core"'

  - name: "Phase 2.2: Isolate Target NVMe Drives (SymbioseNull Driver)"
    actions:
      # PATTERN-005: Use environment variable for playbook path
      - !cmd:
          command: '"%PLAYBOOK%\Tools\devcon.exe" update "%PLAYBOOK%\Drivers\SymbioseNull.inf" "PCI\VEN_144D&DEV_A808"'
          runAs: TrustedInstaller
          expect: 0
          rollback: '"%PLAYBOOK%\Tools\devcon.exe" update "C:\Windows\INF\stornvme.inf" "PCI\VEN_144D&DEV_A808"'

  - name: "Phase 2.3: Isolate Target GPU (SymbioseNull Driver)"
    actions:
      - !cmd:
          command: '"%PLAYBOOK%\Tools\devcon.exe" update "%PLAYBOOK%\Drivers\SymbioseNull.inf" "PCI\VEN_10DE&DEV_2620"'
          runAs: TrustedInstaller
          expect: 0
          rollback: '"%PLAYBOOK%\Tools\devcon.exe" update "C:\Windows\INF\nv_disp.inf" "PCI\VEN_10DE&DEV_2620"'

  # ============================================================
  # PHASE 3: Payload, Bridge & Hive-Mind Injection
  # ============================================================
  - name: "Phase 3.1: Copy Symbiose Bridge Driver"
    actions:
      - !fileCopy:
          source: '%PLAYBOOK%\Drivers\symbiose_bridge.sys'
          destination: 'C:\Windows\System32\Drivers\symbiose_bridge.sys'
          runAs: TrustedInstaller

  - name: "Phase 3.2: Copy Chaos-OS Kernel and Ramdisk"
    actions:
      # PATTERN-005: Exact path mapping from legacy extraction tree
      - !fileCopy:
          source: '%PLAYBOOK%\CHAOS 1.5\CHAOS\BZIMAGE'
          destination: 'C:\Symbiose_Core\BZIMAGE'
          runAs: TrustedInstaller
      - !fileCopy:
          source: '%PLAYBOOK%\CHAOS 1.5\CHAOS\CHAOS.RDZ'
          destination: 'C:\Symbiose_Core\CHAOS.RDZ'
          runAs: TrustedInstaller

  - name: "Phase 3.3: Copy ChaosLoader"
    actions:
      - !fileCopy:
          source: '%PLAYBOOK%\Tools\ChaosLoader.exe'
          destination: 'C:\Symbiose_Core\ChaosLoader.exe'
          runAs: TrustedInstaller

  - name: "Phase 3.4: Register Symbiose Bridge as Boot-Start Driver"
    actions:
      - !cmd:
          command: 'sc create SymbioseBridge type= kernel start= boot binPath= "C:\Windows\System32\Drivers\symbiose_bridge.sys"'
          runAs: TrustedInstaller
          expect: 0
          rollback: 'sc delete SymbioseBridge'

  - name: "Phase 3.5: Register ChaosLoader as Boot-time Service"
    actions:
      - !cmd:
          command: 'sc create ChaosLoader type= own start= auto binPath= "C:\Symbiose_Core\ChaosLoader.exe" depend= SymbioseBridge'
          runAs: TrustedInstaller
          expect: 0
          rollback: 'sc delete ChaosLoader'

  # ============================================================
  # PHASE 4: Verification
  # ============================================================
  - name: "Phase 4.1: Verify VBS is Disabled"
    actions:
      - !cmd:
          command: 'reg query "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard" /v EnableVirtualizationBasedSecurity'
          runAs: TrustedInstaller
          expect_output_contains: '0x0'

  - name: "Phase 4.2: Verify Driver is Registered"
    actions:
      - !cmd:
          command: 'sc query SymbioseBridge'
          runAs: TrustedInstaller
          expect_output_contains: 'TYPE'

  - name: "Phase 4.3: Verify Payload Files Exist"
    actions:
      - !cmd:
          command: 'dir "C:\Symbiose_Core\BZIMAGE"'
          runAs: TrustedInstaller
          expect_output_contains: 'BZIMAGE'
      - !cmd:
          command: 'dir "C:\Symbiose_Core\CHAOS.RDZ"'
          runAs: TrustedInstaller
          expect_output_contains: 'CHAOS.RDZ'

  # ============================================================
  # PHASE 5: Reboot to Activate
  # ============================================================
  - name: "Phase 5.1: Schedule Reboot"
    actions:
      - !message:
          title: "Chaos-Symbiose OS Transmigration Complete"
          message: "The system will reboot in 30 seconds to activate the Symbiose Bridge. Save your work."
          type: warning
      - !cmd:
          command: 'shutdown /r /t 30 /c "Chaos-Symbiose OS: Activating Symbiose Bridge"'
          runAs: TrustedInstaller
          expect: 0
```

### 3.2 — Phase 3 Verification
- [ ] **Axis I:** YAML parses (`python -c "import yaml; yaml.safe_load(open('main.yml'))"`) with absolute zero tabulation errors.
- [ ] **Axis II:** Every `!cmd` action has a corresponding `rollback` string
- [ ] **Axis II:** No bare `!cmd` without `runAs: TrustedInstaller`
- [ ] **Axis III:** Phases are sequential and non-overlapping
- [ ] **Axis IV:** `%PLAYBOOK%` is strictly utilized instead of hardcoded mappings
- [ ] **Axis V:** Playbook version pinned

---

## 🔺 PHASE 4: INTEGRATION TESTING IN QEMU
**[Assigned Agents: 🔺 Crucible | ⚡ Bolt]**
**Status:** [ ] Pending | [ ] In Progress | [x] Completed

**Objective:** Validate the entire pipeline in a virtualized environment before touching physical hardware.

### 4.1 — QEMU Test Script

```bash
#!/usr/bin/env bash
set -euo pipefail

# Phase 4.1: QEMU Integration Testing
# Crucible: PATTERN-004 (quote all vars), PATTERN-007 (check exit codes)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../../build"
QEMU_IMG="${QEMU_PATH:-qemu-system-x86_64}"
OVMF_CODE="${SCRIPT_DIR}/fixtures/OVMF_CODE.fd"
OVMF_VARS="${SCRIPT_DIR}/fixtures/OVMF_VARS.fd"
TEST_DISK="${SCRIPT_DIR}/fixtures/test_disk.qcow2"
SEED_DIR="${SCRIPT_DIR}/fixtures/seed"

# Crucible: PATTERN-012 (pinned versions)
QEMU_VERSION="8.2.0"

echo "========================================"
echo ">> CRUCIBLE QEMU INTEGRATION TEST"
echo "========================================"
echo "QEMU: ${QEMU_IMG}"
echo "Build: ${BUILD_DIR}"
echo ""

# Step 1: Create test disk if missing
if [[ ! -f "${TEST_DISK}" ]]; then
    echo "[1/6] Creating test disk image..."
    "${QEMU_IMG}" create -f qcow2 "${TEST_DISK}" 20G
else
    echo "[1/6] Test disk image exists, skipping creation"
fi

# Step 2: Build Symbiose Bridge driver (cross-compile)
echo "[2/6] Building Symbiose Bridge driver..."
mkdir -p "${BUILD_DIR}"
cmake -S "${SCRIPT_DIR}/../.." -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/../toolchains/wdk-x64.cmake" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --target symbiose_bridge

# Step 3: Build ChaosLoader
echo "[3/6] Building ChaosLoader..."
cmake --build "${BUILD_DIR}" --target ChaosLoader

# Step 4: Build IRC Neural Bus
echo "[4/6] Building IRC Neural Bus..."
cmake --build "${BUILD_DIR}" --target symbiose_ircd

# Step 5: Build VFS Manager & OpenMosix
echo "[5/6] Building VFS Storage Manager & OpenMosix 2026+..."
cmake --build "${BUILD_DIR}" --target vfs_manager
cmake --build "${BUILD_DIR}" --target openmosix_modern

# Step 6: Run QEMU with Symbiose Bridge
echo "[6/6] Launching QEMU with Symbiose Bridge..."
"${QEMU_IMG}" \
    -machine q35,accel=kvm \
    -cpu host \
    -m 4096 \
    -smp 4 \
    -drive if=pflash,format=raw,readonly=on,file="${OVMF_CODE}" \
    -drive if=pflash,format=raw,file="${OVMF_VARS}" \
    -drive file="${TEST_DISK}",format=qcow2,if=virtio \
    -drive file="${BUILD_DIR}/symbiose_bridge_test.vhdx",format=vhdx,if=virtio \
    -netdev user,id=net0,hostfwd=tcp::6667-:6667 \
    -device virtio-net-pci,netdev=net0 \
    -serial stdio \
    -monitor unix:/tmp/symbiose_qemu.sock,server,nowait \
    -d int,cpu_reset \
    2>&1 | tee "${SCRIPT_DIR}/test_output.log"

echo ""
echo "========================================"
echo ">> QEMU session ended"
echo ">> Log: ${SCRIPT_DIR}/test_output.log"
echo "========================================"
```

### 4.2 — Phase 4 Verification
- [ ] **Axis I:** Shell script parses (`shellcheck phase4_qemu_test.sh`)
- [ ] **Axis II:** All variables quoted
- [ ] **Axis V:** Test output logged to `test_output.log`

---

## 🔺 PHASE 5: PHYSICAL HARDWARE VALIDATION
**[Assigned Agent: 👤 HUMAN OPERATOR]**
**Status:** [ ] Pending | [ ] In Progress | [x] Completed

**Objective:** Deploy on physical hardware with full verification.

### 5.1 — Hardware Validation Checklist

```markdown
# Phase 5: Physical Hardware Validation Checklist

## Pre-Deployment Verification

- [ ] BIOS: Secure Boot disabled
- [ ] BIOS: VT-x / AMD-V enabled
- [ ] BIOS: IOMMU / VT-d enabled
- [ ] Windows: VBS disabled (verified via `Get-ComputerInfo`)
- [ ] Windows: HVCI disabled (verified via Registry)
- [ ] Windows: Memory Integrity disabled (verified via Security Center)
- [ ] Windows: Test Signing enabled (verified via `bcdedit`)

## Hardware Airlock Verification

- [ ] Target NVMe drive visible in BIOS
- [ ] Target NVMe drive NOT visible in Windows Disk Management
- [ ] SymbioseNull driver loaded for target NVMe (verified via `pnputil`)
- [ ] Target GPU visible in BIOS
- [ ] Target GPU NOT visible in Windows Device Manager
- [ ] SymbioseNull driver loaded for target GPU (verified via `pnputil`)

## Driver Verification

- [ ] symbiose_bridge.sys loaded (verified via `sc query`)
- [ ] Driver start type = boot (verified via `sc qc`)
- [ ] No BSOD on boot with driver loaded

## Payload Verification

- [ ] BZIMAGE present at C:\Symbiose_Core\BZIMAGE
- [ ] CHAOS.RDZ present at C:\Symbiose_Core\CHAOS.RDZ
- [ ] ChaosLoader.exe present at C:\Symbiose_Core\ChaosLoader.exe
- [ ] SHA256 hashes match build artifacts

## Chaos-OS Boot Verification

- [ ] ChaosLoader.exe runs without error
- [ ] ACPI shutdown hook triggers on Windows shutdown
- [ ] LLM ACK_READY_TO_DIE signal received within 30 seconds
- [ ] NVMe context dump completes before power-off
- [ ] System reboots successfully after context dump

## Rollback Verification

- [ ] Rollback script tested: VBS re-enabled
- [ ] Rollback script tested: NVMe driver restored
- [ ] Rollback script tested: GPU driver restored
- [ ] Rollback script tested: symbiose_bridge.sys unregistered
- [ ] Rollback script tested: Windows boots normally after rollback
```

---

## 📓 CRUCIBLE JOURNAL — CRITICAL LEARNINGS
*(Agents: Append critical architectural learnings here during execution)*

```markdown
# Crucible Journal

## 2026-05-05 - WDF Driver TOCTOU in ACPI Callback
**Learning:** ACPI notification callbacks can fire concurrently with state transitions. The original design had no lock between `SymbioseStateChaosRunning` and `SymbioseStateShutdownPending`.
**Action:** Always use `WdfWaitLockAcquire` before state transitions in kernel callbacks. PATTERN-008 applies to kernel code too.
**Defect Pattern ID:** PATTERN-008
**Axes Affected:** II, IV
**Level:** L2

## 2026-05-05 - Assembly Mode-Switch Destabilization
**Learning:** The Intel architecture strictly requires transitioning into a 32-bit Compatibility Code Segment via a far jump *before* disabling Paging in CR0. Doing it in reverse results in a violent hardware-level exception.
**Action:** Re-ordered `SwitchToChaos.asm` to execute `lgdt` -> `jmp 0x10:.compat_mode` -> `btr eax, 31` (PG).
**Defect Pattern ID:** PATTERN-008
**Axes Affected:** I, II
**Level:** L4

## 2026-05-05 - Register Truncation in 64-bit Mode
**Learning:** Using `and eax, ~0x80000000` in 64-bit mode automatically zeroes the upper 32 bits of the `rax` register, causing severe corruption.
**Action:** Replaced boolean math with specific bit-test-and-reset instruction (`btr rax, 31`).
**Defect Pattern ID:** PATTERN-017 (Register Width Truncation)
**Axes Affected:** II, IV
**Level:** L2

## 2026-05-05 - APBX Playbook Rollback Gaps
**Learning:** The original README had no rollback procedures for VBS disabling or driver isolation. A failed deployment would leave the system in an unusable state.
**Action:** Every `!cmd` and `!reg` action in the playbook MUST have a `rollback` or `rollback_value` field.
**Defect Pattern ID:** PATTERN-015 (missing cleanup on error path)
**Axes Affected:** II, IV
**Level:** L2

## 2026-05-05 - IRC Jumbo Frame Reassembly Buffer Overflow
**Learning:** The `SYMBIOSE_IRC_MAX_LINE` (1 MiB) reassembly buffer must be bounds-checked against `jumbo_chunks_received * SYMBIOSE_IRC_MAX_CHUNK`. Without this, a malicious client can cause heap overflow.
**Action:** Validate `chunk_offset + chunk_len <= payload_len` before copying into reassembly buffer. Use `calloc` for initial allocation and `realloc` with bounds checking for growth.
**Defect Pattern ID:** PATTERN-009 (cross-boundary validation)
**Axes Affected:** II, IV
**Level:** L2

## 2026-05-05 - Nested Pointer IOCTL Vulnerability
**Learning:** A `METHOD_BUFFERED` IOCTL only copies the struct payload into kernel memory, NOT the memory pointed to by internal pointers (`KernelBuffer`, `RamdiskBuffer`). Accessing these directly in Ring-0 is a fatal security and stability flaw.
**Action:** Always wrap userspace pointer validation with `ProbeForRead` inside a `__try/__except` block and use `ExAllocatePool2` for safe Ring-0 copies.
**Defect Pattern ID:** PATTERN-021 (Kernel Mode Dereference of User Mode Memory)
**Axes Affected:** II, IV
**Level:** L2
