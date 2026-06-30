PUBLIC IsrStub0
PUBLIC IsrStub1
PUBLIC IsrStub2
PUBLIC IsrStub3
PUBLIC IsrStub4
PUBLIC IsrStub5
PUBLIC IsrStub6
PUBLIC IsrStub7
PUBLIC IsrStub8
PUBLIC IsrStub9
PUBLIC IsrStub10
PUBLIC IsrStub11
PUBLIC IsrStub12
PUBLIC IsrStub13
PUBLIC IsrStub14
PUBLIC IsrStub15
PUBLIC IsrStub16
PUBLIC IsrStub17
PUBLIC IsrStub18
PUBLIC IsrStub19
PUBLIC IsrStub20
PUBLIC IsrStub21
PUBLIC IsrStub22
PUBLIC IsrStub23
PUBLIC IsrStub24
PUBLIC IsrStub25
PUBLIC IsrStub26
PUBLIC IsrStub27
PUBLIC IsrStub28
PUBLIC IsrStub29
PUBLIC IsrStub30
PUBLIC IsrStub31
PUBLIC IsrStub32
PUBLIC IsrStub33
PUBLIC IsrStub34
PUBLIC IsrStub35
PUBLIC IsrStub36
PUBLIC IsrStub37
PUBLIC IsrStub38
PUBLIC IsrStub39
PUBLIC IsrStub40
PUBLIC IsrStub41
PUBLIC IsrStub42
PUBLIC IsrStub43
PUBLIC IsrStub44
PUBLIC IsrStub45
PUBLIC IsrStub46
PUBLIC IsrStub47
PUBLIC IsrStub48
PUBLIC IsrStub128
PUBLIC IsrCommon

EXTERN IsrHandler:PROC

.CODE

MAKE_ISR_NOERR MACRO num
IsrStub&num PROC
    push 0
    push num
    jmp IsrCommon
IsrStub&num ENDP
ENDM

MAKE_ISR_ERR MACRO num
IsrStub&num PROC
    push num
    jmp IsrCommon
IsrStub&num ENDP
ENDM

MAKE_ISR_NOERR 0
MAKE_ISR_NOERR 1
MAKE_ISR_NOERR 2
MAKE_ISR_NOERR 3
MAKE_ISR_NOERR 4
MAKE_ISR_NOERR 5
MAKE_ISR_NOERR 6
MAKE_ISR_NOERR 7
MAKE_ISR_ERR   8
MAKE_ISR_NOERR 9
MAKE_ISR_ERR   10
MAKE_ISR_ERR   11
MAKE_ISR_ERR   12
MAKE_ISR_ERR   13
MAKE_ISR_ERR   14
MAKE_ISR_NOERR 15
MAKE_ISR_NOERR 16
MAKE_ISR_ERR   17
MAKE_ISR_NOERR 18
MAKE_ISR_NOERR 19
MAKE_ISR_NOERR 20
MAKE_ISR_ERR   21
MAKE_ISR_NOERR 22
MAKE_ISR_NOERR 23
MAKE_ISR_NOERR 24
MAKE_ISR_NOERR 25
MAKE_ISR_NOERR 26
MAKE_ISR_NOERR 27
MAKE_ISR_NOERR 28
MAKE_ISR_NOERR 29
MAKE_ISR_NOERR 30
MAKE_ISR_NOERR 31
MAKE_ISR_NOERR 32
MAKE_ISR_NOERR 33
MAKE_ISR_NOERR 34
MAKE_ISR_NOERR 35
MAKE_ISR_NOERR 36
MAKE_ISR_NOERR 37
MAKE_ISR_NOERR 38
MAKE_ISR_NOERR 39
MAKE_ISR_NOERR 40
MAKE_ISR_NOERR 41
MAKE_ISR_NOERR 42
MAKE_ISR_NOERR 43
MAKE_ISR_NOERR 44
MAKE_ISR_NOERR 45
MAKE_ISR_NOERR 46
MAKE_ISR_NOERR 47

MAKE_ISR_NOERR 48

IsrStub128 PROC
    push 0
    mov r11, 128
    push r11
    jmp IsrCommon
IsrStub128 ENDP

IsrCommon PROC
    push rax
    push rbx
    push rcx
    push rdx
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

    mov ax, 10h
    mov ds, ax
    mov es, ax

    mov rbp, rsp
    mov r12, rbp

    and rsp, -16
    sub rsp, 32

    mov rcx, rbp
    call IsrHandler

    mov rsp, r12

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
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq
IsrCommon ENDP

END
