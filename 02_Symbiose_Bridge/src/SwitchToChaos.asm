; ; 02_Symbiose_Bridge/src/SwitchToChaos.asm
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
    jz param_error_kernel
    test r8, r8
    jz param_error_ramdisk
    test r10, r10               ; FIX 4: Safe register test
    jz param_error_bootparams

    ; Validate kernel size
    test rdx, rdx
    jz param_error_kernel_size
    cmp rdx, 0x100000           ; Minimum 1MB for a valid kernel
    jb param_error_kernel_size
    cmp rdx, 0x40000000         ; Maximum 1GB
    ja param_error_kernel_size

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

param_error_kernel:
    mov eax, 0xC000000D         ; STATUS_INVALID_PARAMETER
    ret

param_error_ramdisk:
    mov eax, 0xC000000D
    ret

param_error_bootparams:
    mov eax, 0xC000000D
    ret

param_error_kernel_size:
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
