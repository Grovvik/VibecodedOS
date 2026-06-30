#include "idt.h"
#include "hal.h"
#include "debug.h"
#include "runtime.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "syscall.h"
#include "framebuffer.h"
#include "process.h"
#include "vmm.h"
#include <intrin.h>

static IdtEntry g_idt[IDT_ENTRIES];

static IrqHandler g_irq_handlers[16];

extern void IsrStub0(void);
extern void IsrStub1(void);
extern void IsrStub2(void);
extern void IsrStub3(void);
extern void IsrStub4(void);
extern void IsrStub5(void);
extern void IsrStub6(void);
extern void IsrStub7(void);
extern void IsrStub8(void);
extern void IsrStub9(void);
extern void IsrStub10(void);
extern void IsrStub11(void);
extern void IsrStub12(void);
extern void IsrStub13(void);
extern void IsrStub14(void);
extern void IsrStub15(void);
extern void IsrStub16(void);
extern void IsrStub17(void);
extern void IsrStub18(void);
extern void IsrStub19(void);
extern void IsrStub20(void);
extern void IsrStub21(void);
extern void IsrStub22(void);
extern void IsrStub23(void);
extern void IsrStub24(void);
extern void IsrStub25(void);
extern void IsrStub26(void);
extern void IsrStub27(void);
extern void IsrStub28(void);
extern void IsrStub29(void);
extern void IsrStub30(void);
extern void IsrStub31(void);
extern void IsrStub32(void);
extern void IsrStub33(void);
extern void IsrStub34(void);
extern void IsrStub35(void);
extern void IsrStub36(void);
extern void IsrStub37(void);
extern void IsrStub38(void);
extern void IsrStub39(void);
extern void IsrStub40(void);
extern void IsrStub41(void);
extern void IsrStub42(void);
extern void IsrStub43(void);
extern void IsrStub44(void);
extern void IsrStub45(void);
extern void IsrStub46(void);
extern void IsrStub47(void);
extern void IsrStub48(void);
extern void IsrStub128(void);

static void* g_isr_stubs[49] = {
    IsrStub0,  IsrStub1,  IsrStub2,  IsrStub3,
    IsrStub4,  IsrStub5,  IsrStub6,  IsrStub7,
    IsrStub8,  IsrStub9,  IsrStub10, IsrStub11,
    IsrStub12, IsrStub13, IsrStub14, IsrStub15,
    IsrStub16, IsrStub17, IsrStub18, IsrStub19,
    IsrStub20, IsrStub21, IsrStub22, IsrStub23,
    IsrStub24, IsrStub25, IsrStub26, IsrStub27,
    IsrStub28, IsrStub29, IsrStub30, IsrStub31,
    IsrStub32, IsrStub33, IsrStub34, IsrStub35,
    IsrStub36, IsrStub37, IsrStub38, IsrStub39,
    IsrStub40, IsrStub41, IsrStub42, IsrStub43,
    IsrStub44, IsrStub45, IsrStub46, IsrStub47,
    IsrStub48
};

static const char* g_exception_names[] = {
    "#DE Divide Error",        "#DB Debug",               "NMI Interrupt",
    "#BP Breakpoint",          "#OF Overflow",            "#BR BOUND Range",
    "#UD Invalid Opcode",      "#NM Device Not Available","#DF Double Fault",
    "Coprocessor Segment",     "#TS Invalid TSS",         "#NP Segment Not Present",
    "#SS Stack Fault",         "#GP General Protection",  "#PF Page Fault",
    "Reserved",                "#MF x87 FPU Error",       "#AC Alignment Check",
    "#MC Machine Check",       "#XM SIMD Exception",      "#VE Virtualization",
    "#CP Control Protection"
};

void IdtSetEntry(i32 idx, void* handler, u16 selector, u8 ist, u8 type_attr) {
    u64 addr = (u64)(usize)handler;
    IdtEntry* e = &g_idt[idx];
    e->offset_low  = (u16)(addr & 0xFFFF);
    e->selector    = selector;
    e->ist         = ist;
    e->type_attr   = type_attr;
    e->offset_mid  = (u16)((addr >> 16) & 0xFFFF);
    e->offset_high = (u32)((addr >> 32) & 0xFFFFFFFF);
    e->reserved   = 0;
}

void KeInitIdt(void) {
    KdPrintf("[IDT] Initializing IDT with %d entries...\n", IDT_ENTRIES);

    RtMemSet(g_idt, 0, sizeof(g_idt));
    RtMemSet(g_irq_handlers, 0, sizeof(g_irq_handlers));

    for (i32 i = 0; i < 32; i++) {
        IdtSetEntry(i, g_isr_stubs[i], KERNEL_CS, 0, 0x8E);
    }

    for (i32 i = 32; i < 48; i++) {
        IdtSetEntry(i, g_isr_stubs[i], KERNEL_CS, 0, 0x8E);
    }

    for (i32 i = 48; i < IDT_ENTRIES; i++) {
        IdtSetEntry(i, g_isr_stubs[48], KERNEL_CS, 0, 0x8E);
    }

    IdtSetEntry(0x80, IsrStub128, KERNEL_CS, 0, 0xEE);

    u16 idt_limit = (u16)(sizeof(g_idt) - 1);
    KdPrintf("[IDT] Loading IDT at %p, limit %u\n", g_idt, idt_limit);
    HalLoadIdt(g_idt, idt_limit);

    KdPrintf("[IDT] IDT initialized OK\n");
}

void KeSetIrqHandler(u8 irq, IrqHandler handler) {
    if (irq < 16) {
        g_irq_handlers[irq] = handler;
    }
}

static const char* GetExceptionName(u64 int_no) {
    if (int_no < 22) return g_exception_names[int_no];
    if (int_no < 32) return "Reserved Exception";
    return NULL;
}

static volatile i32 g_isr_nested = 0;

__declspec(noinline) void IsrHandler(TrapFrame* frame) {
    if (frame->int_no < 32) {
        if (g_isr_nested) {
            while (1) HalHlt();
        }
        g_isr_nested = 1;

        u64 saved_cr3 = __readcr3();
        u64 kernel_cr3 = (u64)(usize)VmmGetPml4();
        if (saved_cr3 != kernel_cr3) {
            __writecr3(kernel_cr3);
        }

        const char* name = GetExceptionName(frame->int_no);
        KdPrintf("\n[ISR] === CPU EXCEPTION %llu ===\n", frame->int_no);
        if (name) KdPrintf("[ISR] Name: %s\n", name);
        KdPrintf("[ISR] Error Code: 0x%llx\n", frame->err_code);
        KdPrintf("[ISR] RIP=0x%llx CS=0x%llx RFLAGS=0x%llx\n",
                 frame->rip, frame->cs, frame->rflags);
        KdPrintf("[ISR] RSP=0x%llx SS=0x%llx\n", frame->rsp, frame->ss);
        KdPrintf("[ISR] RAX=0x%llx RBX=0x%llx RCX=0x%llx RDX=0x%llx\n",
                 frame->rax, frame->rbx, frame->rcx, frame->rdx);
        KdPrintf("[ISR] RSI=0x%llx RDI=0x%llx RBP=0x%llx\n",
                 frame->rsi, frame->rdi, frame->rbp);
        KdPrintf("[ISR] R8=0x%llx R9=0x%llx R10=0x%llx R11=0x%llx\n",
                 frame->r8, frame->r9, frame->r10, frame->r11);
        KdPrintf("[ISR] R12=0x%llx R13=0x%llx R14=0x%llx R15=0x%llx\n",
                 frame->r12, frame->r13, frame->r14, frame->r15);

        KThread* cur_t = PsGetCurrentThread();
        if (cur_t && cur_t->process) {
            KdPrintf("[ISR] Process: PID=%llu '%s' img_base=0x%llx\n",
                     cur_t->process->pid, cur_t->process->name,
                     cur_t->process->image_base);
        }

        i32 is_user = (i32)(frame->cs & 3);
        KdPrintf("[ISR] Mode: %s (CS=0x%llx)\n",
                 is_user ? "USER" : "KERNEL", frame->cs);

        if (is_user && cur_t && cur_t->process) {
            u64 img_base = cur_t->process->image_base;
            u64 img_end = img_base + cur_t->process->image_size;
            u64 rip_off = (frame->rip >= img_base && frame->rip < img_end)
                          ? frame->rip - img_base : 0xFFFFFFFFFFFFFFFFULL;
            KdPrintf("[ISR] RIP offset from image base: 0x%llx (base=0x%llx)\n",
                     rip_off, img_base);
        }

        if (frame->int_no == EXCEPTION_PF) {
            u64 cr2;
            cr2 = __readcr2();
            KdPrintf("[ISR] Page Fault address (CR2): 0x%llx\n", cr2);
            KdPrintf("[ISR] PF flags: P=%d WR=%d US=%d RSVD=%d ID=%d\n",
                     frame->err_code & 1,
                     (frame->err_code >> 1) & 1,
                     (frame->err_code >> 2) & 1,
                     (frame->err_code >> 3) & 1,
                     (frame->err_code >> 4) & 1);
            KdPrintf("[ISR] CR3=0x%llx (saved process CR3)\n", saved_cr3);

            extern void VmmDumpPte(u64* pml4, u64 virt);
            KdPrintf("[ISR] PTE for fault addr in process CR3:\n");
            VmmDumpPte((u64*)PHYS_TO_VIRT(saved_cr3), cr2);
            if (is_user && cur_t && cur_t->process) {
                KdPrintf("[ISR] PTE for fault addr in process page_table:\n");
                VmmDumpPte((u64*)PHYS_TO_VIRT(cur_t->process->page_table), cr2);
            }
            extern u64* VmmGetPml4(void);
            KdPrintf("[ISR] PTE in kernel PML4:\n");
            VmmDumpPte(VmmGetPml4(), cr2);
        }

        {
            u64 fault_rip = frame->rip;
            u64* check_pml4 = (u64*)PHYS_TO_VIRT(saved_cr3);
            u64 fault_page = fault_rip & ~0xFFFULL;
            u64 phys = VmmGetPhysical(check_pml4, fault_page);
            if (phys) {
                u8* instr = (u8*)PHYS_TO_VIRT(phys + (fault_rip & 0xFFF));
                KdPrintf("[ISR] Instruction bytes at RIP:");
                for (i32 b = 0; b < 16; b++) {
                    KdPrintf(" %02x", instr[b]);
                }
                KdPrintf("\n");
            } else {
                KdPrintf("[ISR] RIP page (0x%llx) NOT MAPPED - cannot dump instruction\n",
                         fault_page);
            }
        }

        {
            u64 fault_rsp = frame->rsp;
            u64* rsp_pml4 = (u64*)PHYS_TO_VIRT(saved_cr3);
            u64 rsp_page = fault_rsp & ~0xFFFULL;
            u64 rsp_phys = VmmGetPhysical(rsp_pml4, rsp_page);
            if (rsp_phys) {
                u64* stk = (u64*)PHYS_TO_VIRT(rsp_phys + (fault_rsp & 0xFFF));
                KdPrintf("[ISR] Stack at RSP:");
                for (i32 w = 0; w < 8; w++) {
                    KdPrintf(" 0x%llx", stk[w]);
                }
                KdPrintf("\n");
            }
        }

        if (is_user) {
            FbPrintf("\nProcess crashed (exception %llu at RIP=0x%llx)\n",
                     frame->int_no, frame->rip);
            g_isr_nested = 0;
            SyscallKillCurrentProcess(frame);
            return;
        }

        KdPanic("Unhandled CPU exception %llu at RIP=0x%llx", frame->int_no, frame->rip);
    }

    if (frame->int_no >= 32 && frame->int_no < 48) {
        if (g_isr_nested) {
            /* Nested IRQ during critical kernel section - just ack PIC and return */
            PicSendEoi((u8)(frame->int_no - 32));
            return;
        }
        g_isr_nested = 1;

        u8 irq = (u8)(frame->int_no - 32);

        if (g_irq_handlers[irq]) {
            g_irq_handlers[irq](frame);
        } else {
            KdPrintf("[ISR] Unhandled IRQ %u\n", irq);
        }

        g_isr_nested = 0;
        PicSendEoi(irq);
        return;
    }

    if (frame->int_no == SYSCALL_INT) {
        SyscallIsrHandler(frame);
        return;
    }

    KdPrintf("[ISR] Unhandled interrupt %llu at RIP=0x%llx\n",
             frame->int_no, frame->rip);
}
