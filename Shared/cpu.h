#ifndef _SHARED_CPU_H_
#define _SHARED_CPU_H_

#include "types.h"

#pragma pack(push, 1)
typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 int_no;
    u64 err_code;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
} TrapFrame;
#pragma pack(pop)

typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 rip;
    u64 rsp;
    u64 rflags;
    u64 cr3;
} ThreadContext;

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_CS32 0x18
#define USER_DS   0x20
#define USER_CS64 0x28
#define TSS_SEL   0x38

#define RFLAGS_CF      (1ULL << 0)
#define RFLAGS_PF      (1ULL << 2)
#define RFLAGS_AF      (1ULL << 4)
#define RFLAGS_ZF      (1ULL << 6)
#define RFLAGS_SF      (1ULL << 7)
#define RFLAGS_TF      (1ULL << 8)
#define RFLAGS_IF      (1ULL << 9)
#define RFLAGS_DF      (1ULL << 10)
#define RFLAGS_OF      (1ULL << 11)

#define CR0_PE  (1ULL << 0)
#define CR0_PG  (1ULL << 31)
#define CR4_PAE (1ULL << 5)

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_ACCESSED (1ULL << 5)
#define PAGE_DIRTY    (1ULL << 6)
#define PAGE_NX       (1ULL << 63)

#define EXCEPTION_DE  0
#define EXCEPTION_DB  1
#define EXCEPTION_NMI 2
#define EXCEPTION_BP  3
#define EXCEPTION_OF  4
#define EXCEPTION_BR  5
#define EXCEPTION_UD  6
#define EXCEPTION_NM  7
#define EXCEPTION_DF  8
#define EXCEPTION_TS  10
#define EXCEPTION_NP  11
#define EXCEPTION_SS  12
#define EXCEPTION_GP  13
#define EXCEPTION_PF  14
#define EXCEPTION_MF  16
#define EXCEPTION_AC  17
#define EXCEPTION_MC  18
#define EXCEPTION_XM  19

#define IRQ_TIMER    0
#define IRQ_KEYBOARD 1
#define IRQ_CASCADE  2
#define IRQ_COM2     3
#define IRQ_COM1     4
#define IRQ_ATA1     14
#define IRQ_ATA2     15

#define INT_TIMER    (32 + IRQ_TIMER)
#define INT_KEYBOARD (32 + IRQ_KEYBOARD)

#endif
