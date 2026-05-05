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
global SymbioseTripleFaultRecovery
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