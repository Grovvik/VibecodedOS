.code
extern main:proc
extern tcc_main_entry:proc

public _start
public _tcc_start

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

_tcc_start proc
    mov r8, rdx
    mov rdx, rcx
    mov rcx, rbx
    sub rsp, 32
    call tcc_main_entry
    add rsp, 32
    mov rax, 2
    xor rbx, rbx
    int 80h
    jmp $
_tcc_start endp

end