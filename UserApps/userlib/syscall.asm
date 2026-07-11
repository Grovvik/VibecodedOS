.code

PUBLIC syscall0
PUBLIC syscall1
PUBLIC syscall2
PUBLIC syscall3
PUBLIC syscall4
PUBLIC _setjmp
PUBLIC longjmp
PUBLIC __chkstk

syscall0 proc
    mov rax, rcx
    int 80h
    ret
syscall0 endp

syscall1 proc
    push rbx
    mov rax, rcx
    mov rbx, rdx
    int 80h
    pop rbx
    ret
syscall1 endp

syscall2 proc
    push rbx
    mov rax, rcx
    mov rbx, rdx
    mov rcx, r8
    int 80h
    pop rbx
    ret
syscall2 endp

syscall3 proc
    push rbx
    mov rax, rcx
    mov rbx, rdx
    mov rcx, r8
    mov rdx, r9
    int 80h
    pop rbx
    ret
syscall3 endp

syscall4 proc
    push rbx
    push rsi
    mov rax, rcx
    mov rbx, rdx
    mov rcx, r8
    mov rdx, r9
    mov rsi, [rsp+56]
    int 80h
    pop rsi
    pop rbx
    ret
syscall4 endp

_setjmp proc
    mov [rcx], rbx
    mov [rcx+8], rsp
    mov [rcx+16], rbp
    mov [rcx+24], rdi
    mov [rcx+32], rsi
    mov [rcx+40], r12
    mov [rcx+48], r13
    mov [rcx+56], r14
    mov [rcx+64], r15
    mov rax, [rsp]       ; return address
    mov [rcx+72], rax
    xor rax, rax         ; return 0
    ret
_setjmp endp

longjmp proc
    mov rbx, [rcx]
    mov rsp, [rcx+8]
    mov rbp, [rcx+16]
    mov rdi, [rcx+24]
    mov rsi, [rcx+32]
    mov r12, [rcx+40]
    mov r13, [rcx+48]
    mov r14, [rcx+56]
    mov r15, [rcx+64]
    mov r8,  [rcx+72]    ; return address
    mov [rsp], r8        ; overwrite return address on stack
    mov rax, rdx         ; value to return
    test rax, rax
    jnz label_ret
    mov rax, 1           ; if rdx is 0, return 1
label_ret:
    ret
longjmp endp

__chkstk proc
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
__chkstk endp

end