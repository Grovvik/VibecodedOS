PUBLIC KernelEntry
EXTERN KernelMain:PROC

.DATA
ALIGN 16
g_entry_stack DB 16384 DUP (0)

.CODE
KernelEntry PROC
    mov r8, rcx
    lea rax, g_entry_stack
    add rax, 16384
    mov rsp, rax
    and rsp, -16
    mov rcx, r8
    cld
    cli
    call KernelMain
@@:
    cli
    hlt
    jmp @B
KernelEntry ENDP
END
