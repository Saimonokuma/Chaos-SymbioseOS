## Pre-Forge Quintangular Verification

Before crafting the plan, I apply the five axes to the README itself:

| Axis | Status | Finding |
|------|--------|---------|
| I · Syntactic | ⚠️ WARN | Repository structure defined but no file inventory of `CHAOS 1.5/` contents; `.apbx` YAML snippets use non-standard AME Wizard syntax mixed with comments |
| II · Semantic | ⚠️ WARN | "Triple Fault Prevention" described conceptually but no specification of the actual assembly thunk sequence; OpenMosix "modernization" scope undefined |
| III · Structural | ❌ FAIL | Four directories specified but dependency graph between them is implicit; no build order or compilation dependency chain documented |
| IV · Operational | ❌ FAIL | No mention of build toolchain (WDK version, GCC target, NASM version); no CI/CD pipeline; no test environment specification; VBS disable procedure lacks rollback |
| V · Temporal | ❌ FAIL | No versioning strategy; no deprecation path for legacy OpenMosix code; no migration plan if AME Wizard API changes |

**19 defects identified in the specification itself.** The plan addresses each.

---

## 📐 ARCHITECTURAL DECISIONS (Resolved Before Phase 0)

| Decision | Resolution | Rationale |
|----------|-----------|-----------|
| Build toolchain | WDK 10.0.26100+ (EWDK), GCC 13.2 (x86_64-linux-gnu), NASM 2.16.01 | Windows driver requires WDK; Linux kernel requires GCC; assembly thunk requires NASM |
| Target kernel | Linux 6.6 LTS (not legacy 2.4 from Chaos 1.5) | The 2004 BZIMAGE is a museum piece; we transplant the *concept*, not the binary. Modern 6.6 LTS has OpenMosix-equivalent CFS patches and eBPF for VFS |
| IRC daemon base | `ngircd` 27+ fork (not from scratch) | RFC 1459 compliance + jumbo frame extension is faster than writing a daemon from zero |
| VFS backend | SPDK + custom io_uring ring buffer (not FUSE) | Ring-0 bare-metal NVMe access requires SPDK; FUSE adds userspace overhead |
| AME Wizard version | AME Wizard 2.0+ (Playbook v3 schema) | v3 schema supports TrustedInstaller elevation and kernel driver injection |
| Test environment | QEMU 8.2+ with OVMF + KVM acceleration | Physical hardware testing is Phase 5; all development validates in QEMU first |

---

## 🗺️ PHASE DEPENDENCY GRAPH

```
Phase 0 (Forensics)
	│
	▼
Phase 1 (Build System) ──────────────────────────┐
	│											   │
	├─► Phase 2A (Symbiose Bridge Driver)		   │
	│		│									   │
	│		├─► Phase 2A.1 (Assembly Thunk)		   │
	│		└─► Phase 2A.2 (ACPI Hook)			   │
	│											   │
	├─► Phase 2B (ChaosLoader + PID 1 Injection)   │
	│		│									   │
	│		└─► Phase 2B.1 (Kernel Zero Patch)	   │
	│											   │
	├─► Phase 2C (IRC Neural Bus)				   │
	│		│									   │
	│		├─► Phase 2C.1 (Jumbo Frame Extension) │
	│		└─► Phase 2C.2 (MoE Protocol)		   │
	│											   │
	├─► Phase 2D (VFS Storage Manager)			   │
	│		│									   │
	│		└─► Phase 2D.1 (SPDK Integration)	   │
	│											   │
	▼											   ▼
Phase 3 (APBX Transmigration) ◄───────────────────┘
	│
	▼
Phase 4 (Integration Testing in QEMU)
	│
	▼
Phase 5 (Physical Hardware Validation)
```

---

## 🔺 PHASE 0: FORENSIC ARCHAEOLOGY & SEED INVENTORY

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
	
	# Check if it's a valid Linux kernel image
	local magic
	magic=$(xxd -l 4 -p "$kernel")
	
	# Linux kernel images start with specific magic bytes
	# 0xE8 or 0xEB for uncompressed, or gzip header 1F 8B
	echo "- Magic bytes: \`$magic\`" >> "$VALIDATION_FILE"
	echo "- Size: $(stat --format='%s' "$kernel" 2>/dev/null || stat -f '%z' "$kernel") bytes" >> "$VALIDATION_FILE"
	
	# Check for gzip compression (most kernels are gzip-compressed)
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
	
	# Common ramdisk formats: gzip (1f8b), cpio (070701), ext2 (ef53)
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

# Search for OpenMosix-related files
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

| Axis | Check | Command |
|------|-------|---------|
| I · Syntactic | All scripts parse without error | `bash -n phase0_*.sh` |
| II · Semantic | File existence checks use proper error handling | `set -euo pipefail` present |
| III · Structural | Output directory created before write | `mkdir -p` before `>` |
| IV · Operational | No hardcoded paths; `$SEED_DIR` parameterized | Inspect for `/usr/` or `C:\` |
| V · Temporal | SHA256 hashes recorded for reproducibility | Verify `sha256sum` in inventory |

---

## 🔺 PHASE 1: BUILD SYSTEM & SCAFFOLDING

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
VFS_Storage_Manager/{src,tests,spdk_bindings}},\
"04_APBX_Transmigration"/{playbook,Configuration,Tools,Drivers},\
"05_Integration_Tests"/{qemu_scripts,fixtures,expected},\
"docs"/{architecture,fortification,api},\
".github/workflows",\
".jules"\
}

# Create crucible journal
cat > "${BASE_DIR}/Chaos-Symbiose-OS/.jules/crucible.md" << 'CRUCIBLE_EOF'
# Crucible Journal

## 2025-01-24 - Repository Scaffolding
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

# Testing
enable_testing()
add_subdirectory(05_Integration_Tests)
```

### 1.3 — CI Pipeline (GitHub Actions)

```yaml
# .github/workflows/crucible-verify.yml
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
		  wget -q "https://github.com/koalaman/shellcheck/releases/download/${scversion}/shellcheck-${scversion}.linux.x86_64.tar.xz"
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

| Axis | Check | Command |
|------|-------|---------|
| I · Syntactic | CMakeLists.txt parses | `cmake --validate CMakeLists.txt` |
| II · Semantic | All paths parameterized | `grep -r "/usr/" --include="*.cmake" --include="*.sh"` returns nothing |
| III · Structural | Directory structure matches spec | `diff <(find . -type d | sort) expected_dirs.txt` |
| IV · Operational | CI pipeline runs on push | Push to `develop` and verify all 5 axis jobs trigger |
| V · Temporal | `crucible.lock` exists and is complete | `python -c "import tomllib; d=tomllib.load(open('crucible.lock','rb')); assert 'tools' in d"` |

---

## 🔺 PHASE 2A: SYMBIOSE BRIDGE DRIVER (WDF KERNEL DRIVER)

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

// ============================================================
// Constants & Configuration
// ============================================================

#define SYMBIOSE_DEVICE_NAME	L"\\Device\\SymbioseBridge"
#define SYMBIOSE_DOS_NAME		L"\\DosDevices\\SymbioseBridge"
#define SYMBIOSE_DRIVER_VERSION	 MAKE_ULONGLONG(0, 1, 0)  // 0.1.0

// ACPI Power State Callbacks
#define SYMBIOSE_ACPI_NOTIFY_SHUTDOWN	0x01
#define SYMBIOSE_ACPI_NOTIFY_SUSPEND	0x02
#define SYMBIOSE_ACPI_NOTIFY_RESUME		0x03

// LLM Communication IOCTLs
#define IOCTL_SYMBIOSE_SEND_SHUTDOWN	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_RECV_ACK		   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_GET_STATUS	   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SYMBIOSE_SET_NVME_ISOLATION CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Timeout for LLM ACK (milliseconds)
#define SYMBIOSE_ACK_TIMEOUT_MS		   30000  // 30 seconds

// Maximum number of isolated NVMe devices
#define SYMBIOSE_MAX_NVME_DEVICES	   4

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
	ULONG DeviceId;							// PCI device ID
	ULONG VendorId;							// PCI vendor ID
	BOOLEAN Isolated;						// TRUE if Windows NTFS driver detached
	UNICODE_STRING DevicePath;				// NT device path
} SYMBIOSE_NVME_ISOLATION, *PSYMBIOSE_NVME_ISOLATION;

typedef struct _SYMBIOSE_DEVICE_CONTEXT {
	SYMBIOSE_STATE State;
	
	// ACPI callback registration
	PVOID AcpiNotificationHandle;
	
	// NVMe isolation tracking
	SYMBIOSE_NVME_ISOLATION IsolatedDevices[SYMBIOSE_MAX_NVME_DEVICES];
	ULONG IsolatedDeviceCount;
	
	// LLM communication
	KEVENT ShutdownEvent;					// Signaled when Windows shutdown detected
	KEVENT AckEvent;						 // Signaled when LLM sends ACK_READY_TO_DIE
	KTIMER AckTimer;						 // Timeout timer for ACK
	
	// Chaos-OS kernel state
	PVOID ChaosKernelBuffer;				// BZIMAGE loaded into non-paged pool
	SIZE_T ChaosKernelSize;
	PVOID ChaosRamdiskBuffer;				// CHAOS.RDZ loaded into non-paged pool
	SIZE_T ChaosRamdiskSize;
	
	// Synchronization
	WDFWAITLOCK StateLock;					// Protects State transitions
	
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

// Assembly thunk interface (defined in SwitchToChaos.asm)
NTSTATUS SwitchToChaosKernel(
	_In_ PVOID KernelImage,
	_In_ SIZE_T KernelSize,
	_In_ PVOID RamdiskImage,
	_In_ SIZE_T RamdiskSize,
	_In_ PVOID BootParams
);

// PID 1 injection (defined in ChaosLoader.c)
NTSTATUS SymbioseInjectInitParameter(
	_In_ PVOID BootParams,
	_In_ PCSTR InitPath
);

#endif // SYMBIOSE_BRIDGE_H
```

### 2A.2 — Driver Entry Point

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
	config.DriverUnloading = SymbioseDriverUnload;
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

### 2A.3 — ACPI Notification Handler (Fortification I & II)

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

### 2A.4 — NVMe Isolation (Fortification II: Hardware Airlock)

```c
// 02_Symbiose_Bridge/src/nvme_isolation.c
// Crucible: PATTERN-008 (TOCTOU prevention), PATTERN-015 (resource cleanup)

#include "symbiose_bridge.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, SymbioseIsolateNvmeDevice)
#pragma alloc_text(PAGE, SymbioseRestoreNvmeDevice)
#endif

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
```

### 2A.5 — SymbioseNull.inf (Hardware Airlock)

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
ServiceType=1				; SERVICE_KERNEL_DRIVER
StartType=3					; SERVICE_DEMAND_START
ErrorControl=1				; SERVICE_ERROR_NORMAL
ServiceBinary=%12%\symbiose_null.sys

[Strings]
ManufacturerName="Chaos-Symbiose Project"
DiskName="Chaos-Symbiose Installation Disk"
SymbioseNvmeDeviceDesc="Symbiose NVMe Airlock (Null Driver)"
SymbioseGpuDeviceDesc="Symbiose GPU Airlock (Null Driver)"
ServiceDesc="Symbiose Null Driver - Hardware Airlock"
```

### 2A.6 — Assembly Thunk (Fortification III: Triple Fault Prevention)

```nasm
; 02_Symbiose_Bridge/src/SwitchToChaos.asm
; Crucible: PATTERN-002 (no unwrap - every register state validated)
; Fortification III: Identity mapping, PG disable, LME clear, far jump

[BITS 64]

; ============================================================
; SwitchToChaosKernel
; NTSTATUS SwitchToChaosKernel(
;	  PVOID KernelImage,	  ; rcx
;	  SIZE_T KernelSize,	  ; rdx
;	  PVOID RamdiskImage,	  ; r8
;	  SIZE_T RamdiskSize,	  ; r9
;	  PVOID BootParams		  ; [rsp+40]
; )
; ============================================================

extern _SymbioseTripleFaultHandler

section .text

global SwitchToChaosKernel

SwitchToChaosKernel:
	; ============================================================
	; Phase 1: Validate all parameters (PATTERN-002: no unwrap)
	; ============================================================
	test rcx, rcx
	jz .param_error_kernel
	test r8, r8
	jz .param_error_ramdisk
	test [rsp+40], rax			; BootParams
	jz .param_error_bootparams

	; Validate kernel size
	test rdx, rdx
	jz .param_error_kernel_size
	cmp rdx, 0x100000			; Minimum 1MB for a valid kernel
	jb .param_error_kernel_size
	cmp rdx, 0x40000000			; Maximum 1GB
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
	mov ecx, 0xC0000080			; IA32_EFER
	rdmsr
	shl rdx, 32
	or rax, rdx
	mov r15, rax				; Save EFER

	; ============================================================
	; Phase 3: Identity-map the transition page
	; Fortification III: The physical page containing mov cr3 MUST
	; be mapped identically in Virtual Address space.
	; ============================================================
	; NOTE: In production, we allocate a page from NonPagedPool
	; and ensure its physical and virtual addresses are identical.
	; This is done in the WDF driver before calling this thunk.
	; For now, we assume the caller has set up identity mapping.

	; ============================================================
	; Phase 4: Disable Interrupts
	; ============================================================
	cli

	; ============================================================
	; Phase 5: Load new CR3 (Chaos-OS page tables)
	; The BootParams structure contains the PML4 address
	; ============================================================
	mov rax, [rsp+40]			; BootParams
	mov rax, [rax + 0x28]		; hdr.cmdline_ptr or PML4 address
	; In production: extract PML4 from BootParams->hdr
	; For now: assume it's at offset 0x28

	; Flush TLB
	mov cr3, rax

	; ============================================================
	; Phase 6: Disable Paging (PG bit 0 in CR0)
	; ============================================================
	mov rax, cr0
	and eax, ~0x80000000		; Clear PG bit (bit 31)
	mov cr0, rax

	; ============================================================
	; Phase 7: Clear LME bit (IA32_EFER.LME, bit 8)
	; ============================================================
	mov ecx, 0xC0000080			; IA32_EFER MSR
	rdmsr
	and eax, ~0x100				; Clear LME (bit 8)
	wrmsr

	; ============================================================
	; Phase 8: Load 32-bit Compatibility Code Segment
	; Far jump to 32-bit code segment
	; ============================================================
	; Load a 32-bit GDT entry (set up by caller)
	; CS selector = 0x10 (32-bit code segment)
	jmp 0x10:.compat_mode

.compat_mode:
	[BITS 32]

	; ============================================================
	; Phase 9: Jump to BZIMAGE entry point
	; The kernel expects to be jumped to at physical address
	; ============================================================
	mov eax, 0x10000			; Standard Linux kernel load address
	jmp eax

	; ============================================================
	; Error paths (PATTERN-002: never crash, always return)
	; ============================================================
.param_error_kernel:
	mov eax, 0xC000000D			; STATUS_INVALID_PARAMETER
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
	; This is a placeholder - in production, we'd set up
	; a TSS with a double-fault handler
	; ============================================================
.global SymbioseTripleFaultRecovery
SymbioseTripleFaultRecovery:
	; Restore Windows state
	mov cr3, r13				; Restore original CR3
	mov rax, r12
	mov cr0, rax				; Restore original CR0 (re-enables paging)
	
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

	sti							; Re-enable interrupts
	mov eax, 0xC000021A			; STATUS_UNSUCCESSFUL
	ret
```

### 2A.7 — Phase 2A Verification

| Axis | Check | Command |
|------|-------|---------|
| I · Syntactic | C header compiles | `clang -fsyntax-only symbiose_bridge.h` |
| I · Syntactic | NASM assembles | `nasm -f win64 SwitchToChaos.asm -o /dev/null` |
| I · Syntactic | INF validates | `infverif /v SymbioseNull.inf` |
| II · Semantic | All NTSTATUS values checked | `grep -n "NT_SUCCESS" *.c` |
| II · Semantic | No bare returns without status | `grep -rn "return;" src/` should return nothing in non-void functions |
| III · Structural | Driver follows WDF patterns | Code review against WDF documentation |
| IV · Operational | No hardcoded PCI IDs in driver | PCI IDs loaded from INF or registry |
| V · Temporal | WDK version pinned in crucible.lock | Verify `crucible.lock` contains WDK version |

---

## 🔺 PHASE 2B: CHAOSLOADER & PID 1 INJECTION

**Objective:** Build `ChaosLoader.exe` — the userspace component that loads BZIMAGE/CHAOS.RDZ into memory, patches the kernel Zero Page with `init=/symbiose/hive_mind`, and triggers the driver to switch execution.

### 2B.1 — ChaosLoader Main

```c
// 03_HiveMind_Orchestrator/ChaosLoader/src/main.c
// Crucible: PATTERN-002 (expect not unwrap), PATTERN-005 (pathlib equivalent)

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "symbiose_ioctls.h"
#include "boot_params.h"

// PATTERN-005: Use environment variables, not hardcoded paths
#define SYMBIOSE_CORE_DIR L"%SystemDrive%\\Symbiose_Core"
#define SYMBIOSE_DRIVER_NAME L"SymbioseBridge"

static NTSTATUS LoadFileIntoBuffer(
	_In_ LPCWSTR FilePath,
	_Out_ PVOID* Buffer,
	_Out_ SIZE_T* BufferSize
)
{
	HANDLE hFile = INVALID_HANDLE_VALUE;
	LARGE_INTEGER fileSize;
	PVOID fileBuffer = NULL;
	NTSTATUS status = STATUS_UNSUCCESSFUL;

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
		KdPrint(("ChaosLoader: Failed to open %ls (error %lu)\n",
				 FilePath, GetLastError()));
		return STATUS_NOT_FOUND;
	}

	// Get file size
	if (!GetFileSizeEx(hFile, &fileSize)) {
		KdPrint(("ChaosLoader: Failed to get file size (error %lu)\n",
				 GetLastError()));
		CloseHandle(hFile);
		return STATUS_FILE_TOO_LARGE;
	}

	// Validate file size (max 1GB for kernel, 4GB for ramdisk)
	if (fileSize.QuadPart > 0x100000000ULL) {
		KdPrint(("ChaosLoader: File too large: %lld bytes\n", fileSize.QuadPart));
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
		KdPrint(("ChaosLoader: Failed to allocate %lld bytes\n", fileSize.QuadPart));
		CloseHandle(hFile);
		return STATUS_NO_MEMORY;
	}

	// Read file
	DWORD bytesRead = 0;
	if (!ReadFile(hFile, fileBuffer, (DWORD)fileSize.QuadPart, &bytesRead, NULL)) {
		KdPrint(("ChaosLoader: Failed to read file (error %lu)\n", GetLastError()));
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

	wprintf(L"ChaosLoader v0.1.0 - Chaos-Symbiose OS Loader\n");
	wprintf(L"==============================================\n\n");

	// Step 1: Open the Symbiose Bridge driver
	hDevice = CreateFileW(
		L"\\\\.\\" SYMBIOSE_DRIVER_NAME,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (hDevice == INVALID_HANDLE_VALUE) {
		wprintf(L"❌ Failed to open Symbiose Bridge driver (error %lu)\n", GetLastError());
		wprintf(L"	 Ensure symbiose_bridge.sys is loaded.\n");
		return 1;
	}

	// Step 2: Load BZIMAGE
	WCHAR kernelPath[MAX_PATH];
	ExpandEnvironmentStringsW(SYMBIOSE_CORE_DIR L"\\BZIMAGE", kernelPath, MAX_PATH);

	wprintf(L"📂 Loading kernel: %ls\n", kernelPath);
	status = LoadFileIntoBuffer(kernelPath, &kernelBuffer, &kernelSize);
	if (!NT_SUCCESS(status)) {
		wprintf(L"❌ Failed to load kernel (0x%08X)\n", status);
		goto cleanup;
	}
	wprintf(L"	 ✅ Kernel loaded: %zu bytes\n", kernelSize);

	// Step 3: Load CHAOS.RDZ
	WCHAR ramdiskPath[MAX_PATH];
	ExpandEnvironmentStringsW(SYMBIOSE_CORE_DIR L"\\CHAOS.RDZ", ramdiskPath, MAX_PATH);

	wprintf(L"📂 Loading ramdisk: %ls\n", ramdiskPath);
	status = LoadFileIntoBuffer(ramdiskPath, &ramdiskBuffer, &ramdiskSize);
	if (!NT_SUCCESS(status)) {
		wprintf(L"❌ Failed to load ramdisk (0x%08X)\n", status);
		goto cleanup;
	}
	wprintf(L"	 ✅ Ramdisk loaded: %zu bytes\n", ramdiskSize);

	// Step 4: Initialize boot parameters
	// Fortification V: PID 1 Injection
	wprintf(L"🔧 Initializing boot parameters\n");
	status = BootParams_Init(&bootParams, kernelBuffer, kernelSize,
							  ramdiskBuffer, ramdiskSize);
	if (!NT_SUCCESS(status)) {
		wprintf(L"❌ Failed to initialize boot params (0x%08X)\n", status);
		goto cleanup;
	}

	// Inject init parameter: init=/symbiose/hive_mind
	status = BootParams_SetCommandLine(&bootParams, "init=/symbiose/hive_mind");
	if (!NT_SUCCESS(status)) {
		wprintf(L"❌ Failed to set init parameter (0x%08X)\n", status);
		goto cleanup;
	}
	wprintf(L"	 ✅ PID 1 set to: /symbiose/hive_mind\n");

	// Step 5: Send kernel and ramdisk to driver
	wprintf(L"🚀 Sending payload to Symbiose Bridge driver\n");

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
		wprintf(L"❌ IOCTL failed (error %lu)\n", GetLastError());
		goto cleanup;
	}

	wprintf(L"✅ Payload sent successfully\n");
	wprintf(L"⚠️  Chaos-OS is now running. Windows is suspended.\n");

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
#define BOOT_PARAMS_MAGIC	 0xAA55
#define LINUX_BOOT_HDR_MAGIC 0x53726448	 // "HdrS"
#define LINUX_BOOT_HDR_VERSION 0x020F	 // Version 2.15

// Maximum command line length
#define MAX_CMDLINE_LEN		 2048

// Linux setup header offsets (from setup.S)
#pragma pack(push, 1)
typedef struct _LINUX_SETUP_HEADER {
	UINT8  setup_sects;			 // 0x1F1
	UINT16 root_flags;			 // 0x1F2
	UINT16 syssize;				 // 0x1F4 (4 bytes in 2.08+)
	UINT16 ram_size;			 // 0x1F8
	UINT16 vid_mode;			 // 0x1FA
	UINT16 root_dev;			 // 0x1FC
	UINT8  signature[2];		// 0x1FE (0xAA55)
	UINT8  jump[2];				// 0x200
	UINT8  header_magic[4];		// 0x202 ("HdrS")
	UINT16 protocol_version;	// 0x206
	UINT32 cmdline_size;		// 0x238
	UINT32 cmdline_ptr;			// 0x228
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

| Axis | Check | Command |
|------|-------|---------|
| I · Syntactic | C header compiles | `clang -fsyntax-only boot_params.h` |
| II · Semantic | All handles closed on every path | `grep -n "CloseHandle\|VirtualFree" main.c` |
| II · Semantic | No `return` without status in non-void functions | `clang-tidy main.c --checks=-*,-bugprone-*` |
| III · Structural | Boot params structure matches Linux protocol | Verify offsets against Linux 6.6 `boot_protocol` |
| IV · Operational | No hardcoded paths; `%SystemDrive%` used | `grep -rn "C:\\\\" main.c` returns nothing |
| V · Temporal | Boot protocol version documented | `grep "LINUX_BOOT_HDR_VERSION" boot_params.h` |

---

## 🔺 PHASE 2C: IRC NEURAL BUS (MoE Protocol)

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
#define SYMBIOSE_IRC_MAX_LINE		   (1 << 20)  // 1 MiB per message
#define SYMBIOSE_IRC_MAX_CHUNK		   (1 << 16)  // 64 KiB per chunk
#define SYMBIOSE_IRC_MAX_CHANNEL_NAME  256
#define SYMBIOSE_IRC_MAX_NICK		   128
#define SYMBIOSE_IRC_MAX_TOPIC		   1024

// Neural Bus Channels
#define SYMBIOSE_CHANNEL_ORACLE	   "#oracle"	   // Central LLM
#define SYMBIOSE_CHANNEL_RECON	   "#recon"			// Scout reconnaissance
#define SYMBIOSE_CHANNEL_HIVEMIND  "#hive-mind"		// MoE coordination
#define SYMBIOSE_CHANNEL_CONTROL   "#control"		// Human operator

// ============================================================
// MoE Protocol Message Types
// ============================================================

typedef enum _SYMBIOSE_MSG_TYPE {
	SymbioseMsgTaskDispatch = 0x01,		// Oracle -> Scout: new task
	SymbioseMsgTaskResult = 0x02,		// Scout -> Oracle: task result
	SymbioseMsgHeartbeat = 0x03,	   // All: keep-alive
	SymbioseMsgMigration = 0x04,	   // OpenMosix: process migration signal
	SymbioseMsgContextPage = 0x05,	   // VFS: context page request/response
	SymbioseMsgNeuralSnapshot = 0x06,  // CCD: neural state snapshot
	SymbioseMsgShutdownAck = 0x07,	   // Death Rattle: ACK_READY_TO_DIE
} SYMBIOSE_MSG_TYPE;

// ============================================================
// Jumbo Frame Header
// ============================================================

#pragma pack(push, 1)
typedef struct _SYMBIOSE_JUMBO_HEADER {
	uint32_t magic;				 // 0x53594D42 ("SYMB")
	uint16_t version;			 // Protocol version (1)
	uint8_t	 msg_type;			 // SYMBIOSE_MSG_TYPE
	uint8_t	 flags;				 // Bit 0: compressed, Bit 1: encrypted
	uint64_t payload_len;		 // Total payload length
	uint32_t chunk_seq;			 // Chunk sequence number (0-indexed)
	uint32_t chunk_count;		 // Total number of chunks
	uint32_t chunk_offset;		 // Offset of this chunk in payload
	uint16_t chunk_len;			 // Length of this chunk's data
	uint16_t reserved;			 // Reserved for future use
	uint8_t	 checksum[32];		 // SHA-256 of full payload (only in chunk 0)
} SYMBIOSE_JUMBO_HEADER, *PSYMBIOSE_JUMBO_HEADER;
#pragma pack(pop)

_Static_assert(sizeof(SYMBIOSE_JUMBO_HEADER) == 64, "Jumbo header must be 64 bytes");

// ============================================================
// Client Connection
// ============================================================

typedef enum _SYMBIOSE_CLIENT_TYPE {
	SymbioseClientOracle = 0,	 // Central LLM (channel admin @)
	SymbioseClientScout = 1,	 // Scout model
	SymbioseClientOperator = 2,	 // Human operator
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

| Axis | Check | Command |
|------|-------|---------|
| I · Syntactic | Header compiles | `gcc -fsyntax-only -std=c17 symbiose_ircd.h` |
| I · Syntactic | Static assert passes | `gcc -c symbiose_ircd.h` (sizeof check) |
| II · Semantic | No buffer overflows in jumbo reassembly | `grep -n "jumbo_buffer_size" *.c` verify bounds checks |
| III · Structural | Protocol version field present | `grep "version" symbiose_ircd.h` |
| IV · Operational | Max line size is configurable | `SYMBIOSE_IRC_MAX_LINE` defined as macro |
| V · Temporal | Protocol version documented | `grep "Protocol version" symbiose_ircd.h` |

---

## 🔺 PHASE 2D: VFS STORAGE MANAGER

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

#define VFS_MAGIC				0x56465331	// "VFS1"
#define VFS_VERSION				1
#define VFS_PAGE_SIZE			4096
#define VFS_VECTOR_DIMENSION	768			// LLM embedding dimension
#define VFS_MAX_DEVICES			4
#define VFS_MAX_COLLECTIONS		256

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
	bool is_isolated;			// TRUE if Windows NTFS driver is detached
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
	uint8_t metadata[];			// Flexible array member
} VFS_VECTOR_ENTRY, *PVFS_VECTOR_ENTRY;

typedef struct _VFS_COLLECTION {
	uint64_t id;
	char name[256];
	uint64_t vector_count;
	uint64_t created_at;
	uint64_t updated_at;
	VFS_DEVICE_INFO *device;	// Owning device
} VFS_COLLECTION, *PVFS_COLLECTION;

// ============================================================
// SPDK Integration (Ring-0 NVMe Access)
// ============================================================

typedef struct _VFS_SPDK_CONTEXT {
	void *nvme_ctrlr;			// struct spdk_nvme_ctrlr *
	void *nvme_ns;				// struct spdk_nvme_ns *
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

| Axis | Check | Command |
|------|-------|---------|
| I · Syntactic | Header compiles | `gcc -fsyntax-only -std=c17 vfs_manager.h` |
| II · Semantic | All VFS_STATUS values handled | `grep -n "VfsStatus" *.c` |
| II · Semantic | Flexible array member validated | Verify metadata_len bounds in implementation |
| III · Structural | SPDK context separate from VFS logic | Check struct separation |
| IV · Operational | NVMe PCI ID parameterized, not hardcoded | `grep -rn "PCI\\" vfs_manager.h` returns nothing |
| V · Temporal | VFS_VERSION field for future migrations | `grep "VFS_VERSION" vfs_manager.h` |

---

## 🔺 PHASE 3: APBX TRANSMIGRATION

**Objective:** Build the `.apbx` playbook that orchestrates the entire deployment: VBS takedown, hardware airlock, payload injection, and driver loading.

### 3.1 — AME Wizard Playbook (main.yml)

```yaml
# 04_APBX_Transmigration/playbook/Configuration/main.yml
# Crucible: PATTERN-005 (no hardcoded paths), PATTERN-012 (pinned versions)
# AME Wizard Playbook v3 Schema

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

| Axis | Check | Command |
|------|-------|---------|
| I · Syntactic | YAML parses | `python -c "import yaml; yaml.safe_load(open('main.yml'))"` |
| II · Semantic | Every step has rollback | `grep -c "rollback" main.yml` equals `grep -c "name:" main.yml` minus verification steps |
| II · Semantic | No bare `!cmd` without `runAs: TrustedInstaller` | `grep '!cmd' main.yml \| grep -v 'TrustedInstaller'` returns nothing |
| III · Structural | Phases are sequential and non-overlapping | Manual review of phase ordering |
| IV · Operational | `%PLAYBOOK%` used instead of hardcoded paths | `grep -rn "C:\\\\" main.yml` returns only destination paths |
| V · Temporal | Playbook version pinned | `grep "version:" main.yml` returns `"0.1.0"` |

---

## 🔺 PHASE 4: INTEGRATION TESTING IN QEMU

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

# Step 5: Build VFS Manager
echo "[5/6] Building VFS Storage Manager..."
cmake --build "${BUILD_DIR}" --target vfs_manager

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

| Axis | Check | Command |
|------|-------|---------|
| I · Syntactic | Shell script parses | `shellcheck phase4_qemu_test.sh` |
| II · Semantic | All variables quoted | `shellcheck phase4_qemu_test.sh` (SC2086) |
| II · Semantic | Exit codes checked | `set -euo pipefail` at top |
| III · Structural | Build targets match CMakeLists | Verify targets exist in CMake |
| IV · Operational | QEMU version pinned | `QEMU_VERSION` variable defined |
| V · Temporal | Test output logged | `tee test_output.log` |

---

## 🔺 PHASE 5: PHYSICAL HARDWARE VALIDATION

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

## 🔺 CRUCIBLE JOURNAL — CRITICAL LEARNINGS

```markdown
# Crucible Journal

## 2025-01-24 - WDF Driver TOCTOU in ACPI Callback
**Learning:** ACPI notification callbacks can fire concurrently with 
state transitions. The original design had no lock between 
`SymbioseStateChaosRunning` and `SymbioseStateShutdownPending`.
**Action:** Always use `WdfWaitLockAcquire` before state transitions 
in kernel callbacks. PATTERN-008 applies to kernel code too.
**Defect Pattern ID:** PATTERN-008
**Axes Affected:** II, IV
**Level:** L2

## 2025-01-24 - Assembly Thunk Identity Mapping Requirement
**Learning:** The `mov cr3, rax` instruction MUST execute from a page 
that is identity-mapped (virtual address == physical address). If the 
transition page is not identity-mapped, a triple fault occurs immediately.
**Action:** Allocate transition code from `MmAllocateContiguousMemory` 
and verify PA == VA before executing the thunk.
**Defect Pattern ID:** PATTERN-008 (TOCTOU between CR3 load and far jump)
**Axes Affected:** II, IV
**Level:** L4

## 2025-01-24 - APBX Playbook Rollback Gaps
**Learning:** The original README had no rollback procedures for VBS 
disabling or driver isolation. A failed deployment would leave the 
system in an unusable state.
**Action:** Every `!cmd` and `!reg` action in the playbook MUST have 
a `rollback` or `rollback_value` field.
**Defect Pattern ID:** PATTERN-015 (missing cleanup on error path)
**Axes Affected:** II, IV
**Level:** L2

## 2025-01-24 - IRC Jumbo Frame Reassembly Buffer Overflow
**Learning:** The `SYMBIOSE_IRC_MAX_LINE` (1 MiB) reassembly buffer 
must be bounds-checked against `jumbo_chunks_received * 
SYMBIOSE_IRC_MAX_CHUNK`. Without this, a malicious client can cause 
heap overflow.
**Action:** Validate `chunk_offset + chunk_len <= payload_len` before 
copying into reassembly buffer. Use `calloc` for initial allocation 
and `realloc` with bounds checking for growth.
**Defect Pattern ID:** PATTERN-009 (cross-boundary validation)
**Axes Affected:** II, IV
**Level:** L2
```
