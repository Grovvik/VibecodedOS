#include "types.h"
#include "boot_info.h"
#include "cpu.h"
#include "error.h"

#include "debug.h"
#include "hal.h"
#include "runtime.h"

#include "arch/gdt.h"
#include "arch/idt.h"
#include "arch/pic.h"
#include "arch/pit.h"
#include "arch/syscall.h"

#include "serial.h"
#include "framebuffer.h"
#include "keyboard.h"

#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"

#include "proc/process.h"

#include "fs/ata.h"
#include "fs/ahci.h"
#include "fs/nvme.h"
#include "fs/pci.h"
#include "fs/fat32.h"

#include "audio/audio.h"

#include "net/net.h"

#include "loader/pe_loader.h"
#include <intrin.h>

BootInfo* g_boot_info = NULL;

static void KeInitDisplay(void) {
    KdPrintf("[KE] Initializing display from BootInfo...\n");

    if (!g_boot_info) {
        KdPanic("BootInfo is NULL!");
    }

    KdPrintf("[KE] FB: base=0x%llx %ux%u pitch=%u bpp=%u\n",
             g_boot_info->fb_base,
             g_boot_info->fb_width,
             g_boot_info->fb_height,
             g_boot_info->fb_pitch,
             g_boot_info->fb_bpp);

    if (!g_boot_info->fb_base || !g_boot_info->fb_width || !g_boot_info->fb_height) {
        KdPanic("Framebuffer info missing from BootInfo!");
    }

    KeInitFramebuffer(
        (void*)(usize)g_boot_info->fb_base,
        g_boot_info->fb_width,
        g_boot_info->fb_height,
        g_boot_info->fb_pitch,
        g_boot_info->fb_bpp
    );

    KdPrintf("[KE] Display initialized\n");
}

static void KePrintBanner(void) {
    FbSetColor(FB_CYAN, FB_BLACK);
    FbPrintString("MicroNT Kernel - Phase 2\n");
    FbSetColor(FB_WHITE, FB_BLACK);
    FbPrintf("Kernel base: 0x%llx | Entry: 0x%llx | Size: %llu KB\n",
             g_boot_info->kernel_image_base,
             g_boot_info->kernel_entry_point,
             g_boot_info->kernel_image_size / 1024);
    FbPrintf("Memory: %llu MB | RSDP: 0x%llx\n",
             g_boot_info->total_usable_memory / (1024 * 1024),
             g_boot_info->rsdp);
    FbPutChar('\n');
}

static void KeLaunchShell(void) {
    KdPrintf("[KE] Launching /bin/shell.exe as first user process...\n");

    KProcess* proc = NULL;
    ntstatus status = PeLoadProgram("/bin/shell.exe", &proc);
    if (NT_ERROR(status) || !proc) {
        KdPrintf("[KE] FAILED to load shell.exe! Status=0x%08x\n", status);
        FbSetColor(FB_RED, FB_BLACK);
        FbPrintString("FATAL: Cannot load /bin/shell.exe\n");
        FbSetColor(FB_WHITE, FB_BLACK);
        while (1) HalHlt();
    }

    KThread* thread = proc->threads[0];
    if (!thread) {
        KdPrintf("[KE] FAILED: shell.exe has no threads\n");
        while (1) HalHlt();
    }

    u64 saved_cr3 = __readcr3();
    __writecr3(thread->context.cr3);

    char* sp = (char*)(usize)thread->context.rsp;

    RtMemCopy(sp, "/", 2);
    thread->context.rcx = (u64)(usize)sp;
    sp += 2;

    u64 args_addr_ke = (u64)(usize)sp;
    *sp = 0;
    thread->context.rbx = args_addr_ke;
    thread->context.rdx = 0;

    __writecr3(saved_cr3);

    RtMemCopy(&thread->saved_frame, &(TrapFrame){0}, sizeof(TrapFrame));
    thread->saved_frame.rip = thread->context.rip;
    thread->saved_frame.cs = 0x2B;
    thread->saved_frame.rflags = 0x202;
    thread->saved_frame.rsp = thread->context.rsp;
    thread->saved_frame.ss = 0x23;

    thread->saved_frame.rbx = thread->context.rbx;
    thread->saved_frame.rcx = thread->context.rcx;
    thread->saved_frame.rdx = thread->context.rdx;
    thread->saved_frame.rdi = 0;
    thread->saved_frame.rsi = 0;
    thread->saved_frame.rbp = 0;
    thread->saved_frame.r8  = 0;
    thread->saved_frame.r9  = 0;
    thread->saved_frame.r10 = 0;
    thread->saved_frame.r11 = 0;
    thread->saved_frame.r12 = 0;
    thread->saved_frame.r13 = 0;
    thread->saved_frame.r14 = 0;
    thread->saved_frame.r15 = 0;

    thread->has_saved_frame = 1;
    thread->state = PROCESS_READY;
    proc->state = PROCESS_READY;

    KeSetTssRsp0(thread->kernel_stack_top);
    PsSetCurrentThread(thread);

    UserInitRegs init = { thread->context.rbx, thread->context.rcx, thread->context.rdx };
    HalJumpToUser(thread->context.rip, thread->context.rsp, thread->context.cr3, &init);
}

void KernelMain(BootInfo* boot_info) {
    g_boot_info = boot_info;

    KdInit();
    KdPrintf("\n[KE] ======== MicroNT Kernel Starting v0.3 ========\n");
    KdPrintf("[KE] BootInfo at %p\n", boot_info);
    KdPrintf("[KE] Kernel image base: 0x%llx\n", boot_info->kernel_image_base);
    KdPrintf("[KE] Kernel entry point: 0x%llx\n", boot_info->kernel_entry_point);
    KdPrintf("[KE] Total usable memory: %llu MB\n", boot_info->total_usable_memory / (1024 * 1024));
    KdPrintf("[KE] Framebuffer: %ux%u %ubpp at 0x%llx\n",
             boot_info->fb_width, boot_info->fb_height, boot_info->fb_bpp, boot_info->fb_base);

    KdPrintf("[KE] Step 1: Initializing GDT...\n");
    KeInitGdt();
    KdPrintf("[KE] Step 1: GDT OK\n");

    KdPrintf("[KE] Step 2: Initializing IDT...\n");
    KeInitIdt();
    KdPrintf("[KE] Step 2: IDT OK\n");

    KdPrintf("[KE] Step 3: Initializing PIC...\n");
    KeInitPic();
    KdPrintf("[KE] Step 3: PIC OK\n");

    KdPrintf("[KE] Step 4: Initializing display...\n");
    KeInitDisplay();
    KePrintBanner();
    KdPrintf("[KE] Step 4: Display OK\n");

    KdPrintf("[KE] Step 5: Initializing PIT (100 Hz)...\n");
    KeInitPit(100);
    KdPrintf("[KE] Step 5: PIT OK\n");

    KdPrintf("[KE] Step 6: Initializing keyboard...\n");
    KeInitKeyboard();
    KdPrintf("[KE] Step 6: Keyboard OK\n");

    KdPrintf("[KE] Step 7: Initializing PMM...\n");
    PmmInit(boot_info->memory_map, boot_info->memory_map_size,
            boot_info->memory_map_entry_size, boot_info->memory_map_entry_count);
    KdPrintf("[KE] Step 7: PMM OK\n");

    KdPrintf("[KE] Step 8: Initializing VMM...\n");
    VmmInit();
    KdPrintf("[KE] Step 8: VMM OK\n");

    /* Explicitly map framebuffer as user-accessible (required after VMM hardening) */
    if (g_boot_info && g_boot_info->fb_base) {
        u64 fb_size = (u64)g_boot_info->fb_pitch * g_boot_info->fb_height;
        u64 fb_pages = (fb_size + PAGE_SIZE - 1) / PAGE_SIZE;
        KdPrintf("[KE] Mapping %llu framebuffer pages at 0x%llx for user access\n",
                 fb_pages, g_boot_info->fb_base);
        VmmMapPages(VmmGetPml4(), g_boot_info->fb_base, g_boot_info->fb_base,
                      fb_pages, VMM_USER_FLAGS);
    }

    KdPrintf("[KE] Step 9: Initializing heap...\n");
    HeapInit();
    KdPrintf("[KE] Step 9: Heap OK\n");

    KdPrintf("[KE] Step 10: Initializing process subsystem...\n");
    PsInit();
    KdPrintf("[KE] Step 10: Process OK\n");

    KdPrintf("[KE] Step 11: Initializing disk (PCI/AHCI/NVMe/ATA/FAT)...\n");
    PciInit();
    AhciInit();
    NvmeInit();
    AtaInit();
    Fat32Init(0);
    KdPrintf("[KE] Step 11: Disk OK\n");

    KdPrintf("[KE] Step 12: Initializing audio subsystem...\n");
    AudioInit();
    KdPrintf("[KE] Step 12: Audio OK\n");

    KdPrintf("[KE] Step 13: Initializing network stack...\n");
    /* Default QEMU user networking IP: 10.0.2.15/24, GW: 10.0.2.2 */
    NetInit(0x0F02000A, 0x0202000A, 0x00FFFFFF);
    KdPrintf("[KE] Step 13: Network OK\n");

    KdPrintf("[KE] Step 14: Initializing syscall interface...\n");
    SyscallInit();
    KdPrintf("[KE] Step 14: Syscall OK\n");

    KdPrintf("[KE] Step 14: Registering IRQ handlers...\n");
    KeSetIrqHandler(IRQ_TIMER, PitIrqHandler);
    KeSetIrqHandler(IRQ_KEYBOARD, KeyboardIrqHandler);
    PicUnmaskIrq(IRQ_TIMER);
    PicUnmaskIrq(IRQ_KEYBOARD);
    KdPrintf("[KE] Step 14: IRQ handlers registered\n");

    KdPrintf("[KE] Step 15: Enabling interrupts...\n");
    HalSti();
    KdPrintf("[KE] Step 15: Interrupts enabled\n");

    KdPrintf("[KE] ======== Kernel initialized, launching shell ========\n");

    FbSetColor(FB_GREEN, FB_BLACK);
    FbPrintString("Kernel initialized successfully!\n\n");
    FbSetColor(FB_WHITE, FB_BLACK);

    /* Pin all kernel page-table pages now that every subsystem has been initialized.
       This catches any PDPT/PD/PT pages allocated during HeapInit, PciInit, etc.
       From this point on, PmmFreePage will silently ignore these pages. */
    VmmPinKernelPageTables();

    KeLaunchShell();

    KdPanic("Shell returned - should never happen!");
}
