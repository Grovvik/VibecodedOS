PUBLIC HalAsmLoadGdt
PUBLIC HalAsmLoadIdt
PUBLIC HalAsmLoadTr
PUBLIC HalAsmReloadSegments
PUBLIC HalAsmSetRsp0
PUBLIC HalAsmCli
PUBLIC HalAsmSti
PUBLIC HalAsmDisableInterrupts
PUBLIC HalAsmRestoreInterrupts
PUBLIC HalAsmJumpToUser
PUBLIC HalAsmExitReturn

.DATA
g_exit_rsp dq 0

.CODE

HalAsmLoadGdt PROC
    mov rax, rcx
    lgdt fword ptr [rax]
    ret
HalAsmLoadGdt ENDP

HalAsmLoadIdt PROC
    mov rax, rcx
    lidt fword ptr [rax]
    ret
HalAsmLoadIdt ENDP

HalAsmLoadTr PROC
    ltr cx
    ret
HalAsmLoadTr ENDP

HalAsmReloadSegments PROC
    push 08h
    lea rax, reload_cs
    push rax
    db 048h, 0CBh
reload_cs:
    mov ax, 10h
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
HalAsmReloadSegments ENDP

HalAsmSetRsp0 PROC
    mov [rcx], rdx
    ret
HalAsmSetRsp0 ENDP

HalAsmCli PROC
    cli
    ret
HalAsmCli ENDP

HalAsmSti PROC
    sti
    ret
HalAsmSti ENDP

HalAsmDisableInterrupts PROC
    pushfq
    cli
    pop rax
    ret
HalAsmDisableInterrupts ENDP

HalAsmRestoreInterrupts PROC
    push rcx
    popfq
    ret
HalAsmRestoreInterrupts ENDP

HalAsmJumpToUser PROC
    ; rcx = entry (RIP), rdx = user RSP, r8 = cr3, r9 = init_regs ptr
    ; init_regs: [0]=rbx, [8]=rcx, [16]=rdx

    ; Save current kernel RSP for exit return
    mov qword ptr g_exit_rsp, rsp

    ; Switch to process address space
    mov rax, cr3
    cmp r8, rax
    je skip_cr3
    mov cr3, r8
skip_cr3:

    ; Build iretq frame on current (kernel) stack
    push 23h            ; SS = ring3 data (selector 20h | RPL 3)
    push rdx            ; RSP = user stack pointer

    pushfq              ; push current RFLAGS
    pop rax             ; rax = RFLAGS
    or rax, 200h        ; ensure IF (interrupt enable) bit is set
    push rax            ; RFLAGS with IF=1

    push 2Bh            ; CS = ring3 64-bit code (selector 28h | RPL 3)
    push rcx            ; RIP = entry point

    ; Load user-visible registers from init_regs struct
    mov rbx, [r9]       ; rbx = args
    mov rcx, [r9+8]     ; rcx = cwd
    mov rdx, [r9+16]    ; rdx = argc

    ; Set data segments for ring3
    mov ax, 23h
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    iretq
HalAsmJumpToUser ENDP

HalAsmExitReturn PROC
    mov ax, 10h
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, qword ptr g_exit_rsp
    ret
HalAsmExitReturn ENDP

; __chkstk - stack probe for functions with >4KB locals
; Called with rax = stack allocation size in bytes
; Probes each 4KB page to ensure stack pages are committed
PUBLIC __chkstk
__chkstk PROC
    sub rsp, 10h
    mov [rsp], r10
    mov [rsp+8], r11
    mov r10, rsp
    add r10, 18h        ; original RSP before our sub
    sub r10, rax        ; lowest address we'll touch
    mov r11, rsp
    and r11, -1000h     ; align down to 4KB
    and r10, -1000h     ; align down to 4KB
loop_start:
    cmp r11, r10
    jae loop_done
    sub r11, 1000h
    test [r11], r11     ; probe the page
    jmp loop_start
loop_done:
    mov r10, [rsp]
    mov r11, [rsp+8]
    add rsp, 10h
    ret
__chkstk ENDP

END
