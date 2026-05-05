# Crucible Journal

## 2025-01-24 - Repository Scaffolding
**Learning:** Build system must be established before any kernel code.
**Action:** Always create directory structure, CI, and build tooling first.
**Defect Pattern ID:** PATTERN-005
**Axes Affected:** III, IV, V
**Level:** L5

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

## 2026-05-05 - Internet Knowledge Retrieval
**Learning:** Verified the role of AME Wizard (.apbx playbook executor using TrustedUninstaller backend) and OpenMosix conceptually for scaling AI.
**Action:** No code changes necessary as the concepts are accurately reflected in the YAML syntax and AST generation scripts.
**Defect Pattern ID:** N/A
**Axes Affected:** None
**Level:** Info
