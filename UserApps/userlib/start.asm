.code
extern main:proc

_start proc
    mov r8, rdx
    mov rdx, rcx
    mov rcx, rbx
    sub rsp, 32
    call main
    add rsp, 32
    mov rax, 2
    xor rbx, rbx
    int 80h
    jmp $
_start endp
end