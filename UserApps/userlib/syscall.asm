.code

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

end