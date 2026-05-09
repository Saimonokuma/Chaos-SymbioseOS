; SwitchToChaos.asm — Microsoft x64 ABI compliant VMX thunks
;
; BRIDGE-010: VMLAUNCH entry + VmExitHandler host landing
;
; Reference: Interactive_Plan.md §III·7 (lines 1783-1937) — CANONICAL
;            §XV·4 provides ABI contract; §III·7 wins if they diverge.
;
; Build: ml64.exe /c SwitchToChaos.asm
; Link:  linked into symbiose_bridge.sys
;
; Caller (SymbioseVmLaunch in vmx_hypervisor.c) must have:
;   1. Called SymbioseVmxOn()     — VMXON region active
;   2. Called SymbioseVmcsClear() — VMCS loaded with VMPTRLD
;   3. Called SymbioseVmcsWrite() — all 150+ fields written
;   4. Called SymbioseEptBuild()  — EPT_POINTER set
;
; Returns: RAX = 0 (guest running, VM-Exit diverts to HOST_RIP)
;          RAX = VM_INSTRUCTION_ERROR (1-28) on VMLAUNCH failure
;          RAX = 0xFFFFFFFF on CF=1 (invalid VMCS pointer)

; External C function called by VmExitHandler
; BOOLEAN HandleVmExit(VOID);
;   Returns TRUE  → VMRESUME (continue guest)
;   Returns FALSE → VMXOFF  (shut down VM)
EXTERN HandleVmExit:PROC

PUBLIC SwitchToChaos
PUBLIC VmExitHandler

.CODE

; ─────────────────────────────────────────────────────────────────────────────
; SwitchToChaos — execute VMLAUNCH
;
; Stack on entry: RSP+0 = return address (8 bytes, misaligned)
; x64 ABI: RSP 16-byte aligned at point of CALL. On entry RSP is 8-aligned.
; ─────────────────────────────────────────────────────────────────────────────
SwitchToChaos PROC

    ; 1. Save non-volatile registers (x64 ABI: RBX, RBP, RDI, RSI, R12-R15)
    ;    8 registers × 8 bytes = 64 bytes pushed
    push rbx
    push rbp
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15

    ; 2. Allocate 32-byte shadow space (home space for register args)
    sub rsp, 20h

    ; 3. Execute VMLAUNCH
    ;    Success: CPU transitions to VMX non-root — guest runs from GUEST_RIP.
    ;             This function does NOT return here until VmExitHandler
    ;             calls VMXOFF (which causes ret from this function).
    ;    Failure: ZF=1 (VM_INSTRUCTION_ERROR valid) or CF=1 (no error)
    vmlaunch

    ; ── If we reach here, VMLAUNCH failed ─────────────────────────────────
    ; 4. Check flags BEFORE reading VM_INSTRUCTION_ERROR
    xor rax, rax                ; Default return = 0
    jc  vmlaunch_cf_error       ; CF=1: VMCS invalid, no error field
    jz  vmlaunch_zf_error       ; ZF=1: VMCS valid, error code available
    jmp vmlaunch_cleanup        ; Neither flag: unexpected

vmlaunch_zf_error:
    ; Read VM_INSTRUCTION_ERROR from VMCS (encoding 0x4400)
    mov rcx, 04400h             ; VMCS field encoding: VM_INSTRUCTION_ERROR
    vmread rax, rcx             ; RAX = error code (1-28, Intel SDM Vol 3C §30.4)
    jmp vmlaunch_cleanup

vmlaunch_cf_error:
    mov rax, 0FFFFFFFFh         ; Sentinel: CF=1 means invalid VMCS pointer

vmlaunch_cleanup:
    ; 5. Restore stack and return error code in RAX
    add rsp, 20h
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbp
    pop rbx
    ret                         ; Return VM_INSTRUCTION_ERROR in RAX

SwitchToChaos ENDP

; ─────────────────────────────────────────────────────────────────────────────
; VmExitHandler — CPU lands here on every VM-Exit (HOST_RIP in VMCS)
;
; This is NOT a normal function. The CPU does NOT push a return address.
; RSP = HOST_RSP value set in VMCS (our dedicated host stack).
;
; Contract:
;   1. Save all 15 general-purpose registers (guest state)
;   2. Call HandleVmExit() — C function reads VMCS exit reason
;   3. If HandleVmExit returns TRUE:  VMRESUME (re-enter guest)
;      If HandleVmExit returns FALSE: VMXOFF + ret (back to SymbioseVmLaunch)
; ─────────────────────────────────────────────────────────────────────────────
VmExitHandler PROC

    ; 1. Save all general-purpose registers (guest state is lost if we don't)
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; 2. Shadow space for C call (32 bytes, MS x64 ABI)
    sub rsp, 20h

    ; 3. Call C handler: BOOLEAN HandleVmExit(VOID)
    ;    Returns TRUE  → resume guest (VMRESUME)
    ;    Returns FALSE → shut down VM (VMXOFF)
    call HandleVmExit

    add rsp, 20h

    ; 4. Restore guest GPRs
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    ; 5. Resume guest or exit VMX
    test al, al
    jz   do_vmxoff

    ; VMRESUME — re-enter guest (use for all VM-Exits except shutdown)
    vmresume
    ; If VMRESUME fails, fall through to VMXOFF (catastrophic)

do_vmxoff:
    vmxoff
    ret             ; Returns to SymbioseVmLaunch's cleanup_vmxoff path

VmExitHandler ENDP

END
