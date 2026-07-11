#include "syscall.h"
#include "debug.h"
#include "hal.h"
#include "framebuffer.h"
#include "keyboard.h"
#include "serial.h"
#include "process.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "idt.h"
#include "runtime.h"
#include "cpu.h"
#include "gdt.h"
#include "pit.h"
#include "boot_info.h"
#include "fs/fat32.h"
#include "pe_loader.h"
#include <intrin.h>

extern BootInfo* g_boot_info;

extern KProcess* g_process_list;

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

static void* g_syscall_stubs[] = {
    IsrStub32, IsrStub33, IsrStub34, IsrStub35,
    IsrStub36, IsrStub37, IsrStub38, IsrStub39,
    IsrStub40, IsrStub41, IsrStub42, IsrStub43,
    IsrStub44, IsrStub45, IsrStub46, IsrStub47,
    IsrStub48
};

static u64 g_exit_return_rip;
static u64 g_exit_return_rsp;

void SyscallSetExitReturn(u64 rip, u64 rsp) {
    g_exit_return_rip = rip;
    g_exit_return_rsp = rsp;
}

__declspec(noinline) void SyscallKillCurrentProcess(TrapFrame* frame) {
    KThread* t = PsGetCurrentThread();
    u64 kill_pid = 0;
    if (t && t->process) {
        KdPrintf("[ISR] Killing user process PID=%llu (%s) due to exception\n",
                 t->process->pid, t->process->name);
        kill_pid = t->process->pid;
        t->process->state = PROCESS_TERMINATED;
        t->state = PROCESS_TERMINATED;
    }

    KProcess* p = g_process_list;
    while (p) {
        for (u32 i = 0; i < p->thread_count && i < 16; i++) {
            KThread* wt = p->threads[i];
            if (wt && wt->state == PROCESS_BLOCKED && wt->wait_pid == kill_pid) {
                wt->state = PROCESS_READY;
                wt->wait_pid = 0;
            }
        }
        p = p->next;
    }

    KThread* next = PsScheduleNext();
    if (next && next->has_saved_frame) {
        KdPrintf("[ISR] Switching to TID=%llu PID=%llu RIP=0x%llx RSP=0x%llx CS=0x%llx\n",
                 next->tid, next->process ? next->process->pid : 0,
                 next->saved_frame.rip, next->saved_frame.rsp, next->saved_frame.cs);
        RtMemCopy(frame, &next->saved_frame, sizeof(TrapFrame));
        next->has_saved_frame = 0;
        PsSetCurrentThread(next);
        if (next->process) {
            KeSetTssRsp0(next->kernel_stack_top);
            __writecr3(next->context.cr3);
        }
    } else {
        KdPrintf("[ISR] No thread after kill, halting\n");
        while (1) HalHlt();
    }
}

void SyscallInit(void) {
    u64 star = ((u64)0x18 << 48) | ((u64)0x08 << 32);
    u32 eax = (u32)(star & 0xFFFFFFFF);
    u32 edx = (u32)(star >> 32);
    u32 ecx = 0xC0000081;
    __writemsr(ecx, star);

    KdPrintf("[SYSCALL] System call interface initialized (int 0x%02x, DPL=3) STAR=0x%llx\n", SYSCALL_INT, star);
}

char g_current_directory[FAT_MAX_PATH] = "/";

static void* SysUserPtr(u64 user_addr) {
    if (!user_addr) return NULL;
    return (void*)(usize)user_addr;
}

void SysResolvePath(const char* user_path, char* resolved) {
    if (!user_path) {
        resolved[0] = 0;
        return;
    }

    char normalized[FAT_MAX_PATH];
    usize idx = 0;
    while (user_path[idx] && idx < FAT_MAX_PATH - 1) {
        if (user_path[idx] == '\\') {
            normalized[idx] = '/';
        } else {
            normalized[idx] = user_path[idx];
        }
        idx++;
    }
    normalized[idx] = 0;

    if (normalized[0] == '/') {
        RtStrCopy(resolved, normalized);
        return;
    }

    const char* src = normalized;
    if (src[0] == '.' && (src[1] == '/' || src[1] == '\0')) {
        src++;
        while (*src == '/') src++;
    }

    usize cwd_len = RtStrLen(g_current_directory);
    if (cwd_len == 1 && g_current_directory[0] == '/') {
        resolved[0] = '/';
        RtStrCopy(resolved + 1, src);
    } else {
        RtStrCopy(resolved, g_current_directory);
        if (resolved[cwd_len - 1] != '/') {
            resolved[cwd_len] = '/';
            resolved[cwd_len + 1] = 0;
        }
        RtStrConcat(resolved, src);
    }
}

static FatDirEntry* g_dir_entries;
static u32 g_dir_count;
static u32 g_dir_index;

static u8* g_file_data;
static u32 g_file_size;
static u32 g_file_pos;

static KFileDesc g_fd_table[MAX_FD];

__declspec(noinline) void SyscallIsrHandler(TrapFrame* frame) {
    u64 syscall_num = frame->rax;
    u64 ret = 0;

    KThread* cur_t = PsGetCurrentThread();
    u64 cur_pid = cur_t && cur_t->process ? cur_t->process->pid : 0;
    //if (syscall_num != SYS_GETCHAR && syscall_num != SYS_PUTCHAR && syscall_num != SYS_PUTS) {
    //    KdPrintf("[SYSCALL] PID=%llu syscall(%llu) rbx=0x%llx rcx=0x%llx rdx=0x%llx\n",
    //             cur_pid, syscall_num, frame->rbx, frame->rcx, frame->rdx);
    //}

    switch (syscall_num) {
    case SYS_EXIT: {
        KThread* t = PsGetCurrentThread();
        u64 exit_pid = 0;
        if (t && t->process) {
            exit_pid = t->process->pid;
            KdPrintf("[SYSCALL] PID=%llu exit(%llu)\n", t->process->pid, frame->rbx);
            t->process->state = PROCESS_TERMINATED;
            t->state = PROCESS_TERMINATED;
        }

        KProcess* p = g_process_list;
        while (p) {
            for (u32 i = 0; i < p->thread_count && i < 16; i++) {
                KThread* wt = p->threads[i];
                if (wt && wt->state == PROCESS_BLOCKED && wt->wait_pid == exit_pid) {
                    wt->state = PROCESS_READY;
                    wt->wait_pid = 0;
                }
            }
            p = p->next;
        }

        KThread* next = PsScheduleNext();
        if (next && next->has_saved_frame) {
            KdPrintf("[SYSCALL] EXIT: switching to TID=%llu PID=%llu RIP=0x%llx CS=0x%llx\n",
                     next->tid, next->process ? next->process->pid : 0,
                     next->saved_frame.rip, next->saved_frame.cs);
            KdPrintf("[SYSCALL] EXIT: frame ptr=%p, saved_frame ptr=%p\n", frame, &next->saved_frame);
            KdPrintf("[SYSCALL] EXIT: BEFORE copy: frame->rip=0x%llx frame->cs=0x%llx\n", frame->rip, frame->cs);
            RtMemCopy(frame, &next->saved_frame, sizeof(TrapFrame));
            KdPrintf("[SYSCALL] EXIT: AFTER copy: frame->rip=0x%llx frame->cs=0x%llx frame->rsp=0x%llx frame->ss=0x%llx\n",
                     frame->rip, frame->cs, frame->rsp, frame->ss);
            next->has_saved_frame = 0;
            PsSetCurrentThread(next);
            if (next->process) {
                KeSetTssRsp0(next->kernel_stack_top);
                __writecr3(next->context.cr3);
            }
        } else {
            KdPrintf("[SYSCALL] EXIT: no thread to switch to, halting\n");
            while (1) HalHlt();
        }

        return;
    }
    case SYS_GETPID: {
        KThread* t = PsGetCurrentThread();
        ret = t && t->process ? t->process->pid : 0;
        break;
    }
    case SYS_PUTS: {
        const char* s = (const char*)SysUserPtr(frame->rbx);
        if (s) {
            FbPrintString(s);
            for (const char* p = s; *p; p++) SerialPutChar(*p);
        }
        break;
    }
    case SYS_GETCHAR: {
        if (KeyboardHasChar()) {
            ret = (u64)KeyboardGetChar();
        } else {
            ret = 0;
        }
        break;
    }
    case SYS_CLEAR: {
        FbClear();
        break;
    }
    case SYS_WRITE: {
        if (frame->rbx == 1) {
            const char* s = (const char*)SysUserPtr(frame->rcx);
            if (s) {
                for (u64 i = 0; i < frame->rdx; i++) {
                    if (s[i] == '\n') FbPutChar('\r');
                    FbPutChar(s[i]);
                }
            }
        }
        ret = frame->rdx;
        break;
    }
    case SYS_MMAP: {
        KThread* t = PsGetCurrentThread();
        if (!t || !t->process) { ret = 0; break; }
        u64 size = (frame->rbx + PAGE_SIZE - 1) & ~0xFFFULL;
        u64 pages = size / PAGE_SIZE;
        u64* pml4 = (u64*)PHYS_TO_VIRT(t->process->page_table);
        u64 virt = t->process->heap_brk;

        for (u64 i = 0; i < pages; i++) {
            u64 phys = PmmAllocPage();
            if (!phys) { ret = 0; break; }
            VmmMapPage(pml4, virt + i * PAGE_SIZE, phys, VMM_USER_FLAGS);
        }

        t->process->heap_brk = virt + size;
        ret = virt;
        break;
    }

    case SYS_SYSTEM: {
        u64 cmd = frame->rbx;
        switch (cmd) {
        case SYS_SYS_REBOOT: {
            FbPrintString("Rebooting...\n");
            KdPrintf("[SYSCALL] Reboot requested\n");
            HalCli();
            HalIoWait();
            HalOutByte(0x64, 0xFE);
            HalIoWait();
            while (1) HalHlt();
            break;
        }
        case SYS_SYS_HALT: {
            FbPrintString("System halted.\n");
            KdPrintf("[SYSCALL] System halt requested\n");
            HalCli();
            while (1) HalHlt();
            break;
        }
        case SYS_SYS_TICKS: {
            ret = KeGetTickCount();
            break;
        }
        case SYS_SYS_MEMINFO: {
            if (g_boot_info) {
                ret = g_boot_info->total_usable_memory;
            }
            break;
        }
        case SYS_SYS_PS: {
            void* ubuf = SysUserPtr(frame->rcx);
            u32 max_count = (u32)frame->rdx;
            if (!ubuf) { ret = 0; break; }
            PsInfoEntry* entries = (PsInfoEntry*)ubuf;
            if (max_count > PS_INFO_MAX) max_count = PS_INFO_MAX;
            u32 idx = 0;
            KProcess* p = g_process_list;
            while (p && idx < max_count) {
                if (p->state != PROCESS_TERMINATED) {
                    RtMemSet(entries[idx].name, 0, 64);
                    RtStrnCopy(entries[idx].name, p->name, 63);
                    entries[idx].pid = p->pid;
                    entries[idx].state = (i32)p->state;
                    entries[idx].thread_count = p->thread_count;
                    idx++;
                }
                p = p->next;
            }
            ret = (u64)idx;
            break;
        }
        default:
            KdPrintf("[SYSCALL] Unknown SYS_SYSTEM subcommand %llu\n", cmd);
            ret = (u64)-1;
            break;
        }
        break;
    }
    case SYS_SETCOLOR: {
        FbSetColor((u32)frame->rbx, (u32)frame->rcx);
        break;
    }
    case SYS_GETCWD: {
        void* buf = SysUserPtr(frame->rbx);
        if (!buf) { ret = (u64)-1; break; }
        usize cwd_len = RtStrLen(g_current_directory);
        usize buf_size = (usize)frame->rcx;
        if (cwd_len + 1 > buf_size) { ret = (u64)-1; break; }
        RtMemCopy(buf, g_current_directory, cwd_len + 1);
        ret = 0;
        break;
    }
    case SYS_FS_OPENDIR: {
        if (g_dir_entries) { KmFree(g_dir_entries); g_dir_entries = NULL; }
        g_dir_count = 0;
        g_dir_index = 0;

        u32 fat_count = 0;
        FatDirEntry* fat_buf = NULL;

        ntstatus status = STATUS_UNSUCCESSFUL;

        if (frame->rbx != 0) {
            const char* path = (const char*)SysUserPtr(frame->rbx);
            if (!path) { ret = (u64)-1; break; }
            char resolved[FAT_MAX_PATH];
            SysResolvePath(path, resolved);
            status = Fat32OpenPath(resolved);
        } else {
            if (g_current_directory[0] == '/' && g_current_directory[1] == 0)
                status = Fat32OpenRoot();
            else
                status = Fat32OpenPath(g_current_directory);
        }

        if (NT_SUCCESS(status)) {
            fat_buf = (FatDirEntry*)KmAlloc(sizeof(FatDirEntry) * 256);
            if (fat_buf) {
                u32 count = 256;
                ntstatus rs = Fat32ReadDir(fat_buf, &count);
                Fat32Close();
                if (NT_SUCCESS(rs)) fat_count = count;
                else { KmFree(fat_buf); fat_buf = NULL; }
            }
        }

        if (fat_count == 0) {
            if (fat_buf) KmFree(fat_buf);
            ret = (u64)-1; break;
        }

        g_dir_entries = fat_buf;
        g_dir_count = fat_count;
        ret = 0;
        break;
    }
    case SYS_FS_READDIR: {
        if (!g_dir_entries || g_dir_index >= g_dir_count) {
            ret = 1;
            break;
        }
        void* buf = SysUserPtr(frame->rbx);
        if (!buf) { ret = (u64)-1; break; }

        char* dst = (char*)buf;
        const char* src = g_dir_entries[g_dir_index].name;
        usize nlen = RtStrLen(src);
        if (nlen >= 256) nlen = 255;
        RtMemCopy(dst, src, nlen);
        dst[nlen] = 0;
        *(u32*)(dst + 256) = g_dir_entries[g_dir_index].file_size;
        *(i32*)(dst + 260) = g_dir_entries[g_dir_index].is_directory;

        g_dir_index++;
        ret = 0;
        break;
    }
    case SYS_FS_CLOSEDIR: {
        if (g_dir_entries) { KmFree(g_dir_entries); g_dir_entries = NULL; }
        g_dir_count = 0;
        g_dir_index = 0;
        ret = 0;
        break;
    }
    case SYS_FS_OPENFILE: {
        if (g_file_data) { KmFree(g_file_data); g_file_data = NULL; }
        g_file_size = 0;
        g_file_pos = 0;

        if (frame->rbx == 0) { ret = (u64)-1; break; }
        const char* path = (const char*)SysUserPtr(frame->rbx);
        if (!path) { ret = (u64)-1; break; }

        char resolved[FAT_MAX_PATH];
        SysResolvePath(path, resolved);

        ntstatus status = Fat32OpenPath(resolved);
        if (NT_SUCCESS(status)) {
            u32 size = Fat32GetFileSize();
            if (size == 0 || size > 16777216) {
                Fat32Close();
                ret = (u64)-1; break;
            }
            g_file_data = (u8*)KmAlloc(size);
            if (!g_file_data) { Fat32Close(); ret = (u64)-1; break; }
            u32 bytes_read = 0;
            status = Fat32ReadFile(g_file_data, &bytes_read);
            Fat32Close();
            if (NT_ERROR(status)) {
                KmFree(g_file_data); g_file_data = NULL;
                ret = (u64)-1; break;
            }
            g_file_size = bytes_read;
            ret = 0;
            break;
        }

        ret = (u64)-1;
        break;
    }
    case SYS_FS_READFILE: {
        if (!g_file_data) { ret = (u64)-1; break; }
        void* buf = SysUserPtr(frame->rbx);
        if (!buf) { ret = (u64)-1; break; }
        u32 count = (u32)frame->rcx;
        u32 remaining = g_file_size - g_file_pos;
        if (count > remaining) count = remaining;
        if (count > 0) {
            RtMemCopy(buf, g_file_data + g_file_pos, count);
            g_file_pos += count;
        }
        ret = (u64)count;
        break;
    }
    case SYS_FS_FILESIZE: {
        ret = (u64)g_file_size;
        break;
    }
    case SYS_FS_CLOSEFILE: {
        if (g_file_data) { KmFree(g_file_data); g_file_data = NULL; }
        g_file_size = 0;
        g_file_pos = 0;
        ret = 0;
        break;
    }
    case SYS_FS_WRITEFILE: {
        if (frame->rbx == 0 || frame->rcx == 0 || frame->rdx == 0) {
            ret = (u64)-1; break;
        }
        const char* wpath = (const char*)SysUserPtr(frame->rbx);
        const void* wdata = (const void*)SysUserPtr(frame->rcx);
        u32 wsize = (u32)frame->rdx;
        if (!wpath || !wdata) { ret = (u64)-1; break; }

        char resolved[FAT_MAX_PATH];
        SysResolvePath(wpath, resolved);

        ntstatus ws = Fat32WriteFile(resolved, wdata, wsize);
        ret = NT_SUCCESS(ws) ? 0 : (u64)-1;
        break;
    }
    case SYS_FS_DELETE: {
        const char* dpath = (const char*)SysUserPtr(frame->rbx);
        if (!dpath) { ret = (u64)-1; break; }
        char resolved[FAT_MAX_PATH];
        SysResolvePath(dpath, resolved);
        ntstatus ds = Fat32DeleteFile(resolved);
        ret = NT_SUCCESS(ds) ? 0 : (u64)-1;
        break;
    }
    case SYS_FS_MKDIR: {
        const char* mpath = (const char*)SysUserPtr(frame->rbx);
        if (!mpath) { ret = (u64)-1; break; }
        char resolved[FAT_MAX_PATH];
        SysResolvePath(mpath, resolved);
        ntstatus ms = Fat32CreateDirectory(resolved);
        ret = NT_SUCCESS(ms) ? 0 : (u64)-1;
        break;
    }
    case SYS_EXEC: {
        const char* epath = (const char*)SysUserPtr(frame->rbx);
        if (!epath) { ret = (u64)-1; break; }

        char resolved[FAT_MAX_PATH];
        SysResolvePath(epath, resolved);

        KdPrintf("[SYSCALL] EXEC: path='%s' resolved='%s'\n", epath, resolved);

        KProcess* proc = NULL;
        ntstatus status = PeLoadProgram(resolved, &proc);
        if (NT_ERROR(status) || !proc) { ret = (u64)-1; break; }

        KThread* child = proc->threads[0];
        if (!child) { PsDestroyProcess(proc); ret = (u64)-1; break; }

        const char* eargs_src = (const char*)SysUserPtr(frame->rcx);
        char exec_args[512];
        exec_args[0] = 0;
        if (eargs_src && *eargs_src) {
            u32 alen = (u32)RtStrLen(eargs_src);
            if (alen > 500) alen = 500;
            RtMemCopy(exec_args, eargs_src, alen);
            exec_args[alen] = 0;
        }

        u64 saved_cr3 = __readcr3();
        __writecr3(child->context.cr3);

        char* sp = (char*)(usize)child->context.rsp;

        u32 cwd_len = (u32)RtStrLen(g_current_directory);
        RtMemCopy(sp, g_current_directory, cwd_len + 1);
        child->context.rcx = (u64)(usize)sp;
        sp += cwd_len + 1;

        u64 args_addr = (u64)(usize)sp;
        if (exec_args[0]) {
            u32 alen = (u32)RtStrLen(exec_args);
            RtMemCopy(sp, exec_args, alen);
            sp += alen;
        }
        *sp = 0;
        child->context.rbx = args_addr;
        child->context.rdx = 0;

        KdPrintf("[SYSCALL] EXEC: args='%s' cwd='%s'\n",
                 exec_args, g_current_directory);

        __writecr3(saved_cr3);

        RtMemSet(&child->saved_frame, 0, sizeof(TrapFrame));
        child->saved_frame.rip = child->context.rip;
        child->saved_frame.cs = 0x2B;
        child->saved_frame.rflags = 0x202;
        child->saved_frame.rsp = child->context.rsp;
        child->saved_frame.ss = 0x23;

        child->saved_frame.rbx = child->context.rbx;
        child->saved_frame.rcx = child->context.rcx;
        child->saved_frame.rdx = child->context.rdx;
        child->saved_frame.rdi = 0;
        child->saved_frame.rsi = 0;
        child->saved_frame.rbp = 0;
        child->saved_frame.r8  = 0;
        child->saved_frame.r9  = 0;
        child->saved_frame.r10 = 0;
        child->saved_frame.r11 = 0;
        child->saved_frame.r12 = 0;
        child->saved_frame.r13 = 0;
        child->saved_frame.r14 = 0;
        child->saved_frame.r15 = 0;

        child->has_saved_frame = 1;
        child->state = PROCESS_READY;
        proc->state = PROCESS_READY;

        KThread* parent = PsGetCurrentThread();
        if (parent && parent->process) {
            parent->state = PROCESS_BLOCKED;
            parent->wait_pid = proc->pid;
            parent->wakeup_tick = 0;

            RtMemCopy(&parent->saved_frame, frame, sizeof(TrapFrame));
            parent->has_saved_frame = 1;

            parent->saved_frame.rax = (u64)proc->pid;

            KdPrintf("[SYSCALL] EXEC: parent PID=%llu blocked, switching to child PID=%llu\n",
                     parent->process->pid, proc->pid);

            KdPrintf("[SYSCALL] EXEC: child frame: rip=0x%llx cs=0x%llx rflags=0x%llx rsp=0x%llx ss=0x%llx\n",
                     child->saved_frame.rip, child->saved_frame.cs, child->saved_frame.rflags,
                     child->saved_frame.rsp, child->saved_frame.ss);

            RtMemCopy(frame, &child->saved_frame, sizeof(TrapFrame));
            child->has_saved_frame = 0;
            PsSetCurrentThread(child);
            KeSetTssRsp0(child->kernel_stack_top);
            __writecr3(child->context.cr3);
            KdPrintf("[SYSCALL] EXEC: about to iretq, frame->rip=0x%llx frame->cs=0x%llx frame->rsp=0x%llx\n",
                     frame->rip, frame->cs, frame->rsp);
            return;
        }

        ret = (u64)proc->pid;
        break;
    }
    case SYS_SLEEP: {
        KThread* t = PsGetCurrentThread();
        if (!t || !t->process) { ret = (u64)-1; break; }
        u64 ms = frame->rbx;
        u64 ticks = ms / 10;
        if (ticks == 0) ticks = 1;
        extern u64 KeGetTickCount(void);
        t->wakeup_tick = KeGetTickCount() + ticks;
        t->state = PROCESS_BLOCKED;

        RtMemCopy(&t->saved_frame, frame, sizeof(TrapFrame));
        t->has_saved_frame = 1;

        KThread* next = PsScheduleNext();
        if (next && next->has_saved_frame) {
            RtMemCopy(frame, &next->saved_frame, sizeof(TrapFrame));
            next->has_saved_frame = 0;
            PsSetCurrentThread(next);
            if (next->process) {
                KeSetTssRsp0(next->kernel_stack_top);
                __writecr3(next->context.cr3);
            }
        } else {
            t->state = PROCESS_READY;
            t->has_saved_frame = 0;
        }
        return;
    }
    case SYS_CHDIR: {
        const char* cpath = (const char*)SysUserPtr(frame->rbx);
        if (!cpath) { ret = (u64)-1; break; }
        char resolved[FAT_MAX_PATH];
        SysResolvePath(cpath, resolved);
        ntstatus cs = Fat32OpenPath(resolved);
        if (NT_ERROR(cs)) { ret = (u64)-1; break; }
        Fat32Close();
        RtStrCopy(g_current_directory, resolved);
        ret = 0;
        break;
    }
    case SYS_PUTCHAR: {
        char c = (char)frame->rbx;
        FbPutChar(c);
        SerialPutChar(c);
        ret = 0;
        break;
    }
    case SYS_FB_MAP: {
        KThread* t = PsGetCurrentThread();
        if (!t || !t->process || !g_boot_info) { ret = 0; break; }
        FbInfo* uinfo = (FbInfo*)SysUserPtr(frame->rbx);
        if (!uinfo) { ret = (u64)-1; break; }

        u64 fb_phys = g_boot_info->fb_base;
        u32 fb_pitch = g_boot_info->fb_pitch;
        u32 fb_height = g_boot_info->fb_height;
        u32 fb_bpp = g_boot_info->fb_bpp;
        u64 fb_size = (u64)fb_pitch * fb_height;
        u64 fb_pages = (fb_size + PAGE_SIZE - 1) / PAGE_SIZE;

        u64* pml4 = (u64*)PHYS_TO_VIRT(t->process->page_table);
        u64 fb_virt = t->process->heap_brk;

        for (u64 i = 0; i < fb_pages; i++) {
            VmmMapPage(pml4, fb_virt + i * PAGE_SIZE, fb_phys + i * PAGE_SIZE, VMM_USER_FLAGS | VMM_WRITABLE);
        }
        t->process->heap_brk = fb_virt + fb_pages * PAGE_SIZE;

        FbInfo info;
        info.fb_virt = fb_virt;
        info.width = g_boot_info->fb_width;
        info.height = fb_height;
        info.pitch = fb_pitch;
        info.bpp = fb_bpp;

        u64 saved_cr3 = __readcr3();
        __writecr3(t->process->page_table);
        RtMemCopy(uinfo, &info, sizeof(FbInfo));
        __writecr3(saved_cr3);

        ret = 0;
        break;
    }
    case SYS_GETKEY: {
        extern i32 KeyboardGetKeyEvent(KeyEvent* ev);
        KeyEvent* uev = (KeyEvent*)SysUserPtr(frame->rbx);
        if (!uev) { ret = (u64)-1; break; }

        KeyEvent ev;
        i32 got = KeyboardGetKeyEvent(&ev);
        if (got) {
            u64 saved_cr3 = __readcr3();
            KThread* t = PsGetCurrentThread();
            if (t && t->process) __writecr3(t->process->page_table);
            RtMemCopy(uev, &ev, sizeof(KeyEvent));
            if (t && t->process) __writecr3(saved_cr3);
            ret = 1;
        } else {
            ret = 0;
        }
        break;
    }
    case SYS_OPEN: {
        const char* opath = (const char*)SysUserPtr(frame->rbx);
        if (!opath) { ret = (u64)-1; break; }

        char resolved[FAT_MAX_PATH];
        SysResolvePath(opath, resolved);

        ntstatus status = Fat32OpenPath(resolved);
        if (NT_ERROR(status)) { ret = (u64)-1; break; }

        u32 fsize = Fat32GetFileSize();
        Fat32Close();

        if (fsize == 0 || fsize > 16777216) { ret = (u64)-1; break; }

        i32 fd = -1;
        for (i32 i = 0; i < MAX_FD; i++) {
            if (!g_fd_table[i].used) {
                fd = i;
                break;
            }
        }
        if (fd < 0) { ret = (u64)-1; break; }

        u64 path_len = RtStrLen(resolved) + 1;
        char* kpath = (char*)KmAlloc(path_len);
        if (!kpath) { ret = (u64)-1; break; }
        RtMemCopy(kpath, resolved, path_len);

        g_fd_table[fd].path = kpath;
        g_fd_table[fd].size = fsize;
        g_fd_table[fd].pos = 0;
        g_fd_table[fd].used = 1;
        ret = (u64)fd;
        break;
    }
    case SYS_READ: {
        i32 fd = (i32)frame->rbx;
        if (fd < 0 || fd >= MAX_FD || !g_fd_table[fd].used) { ret = (u64)-1; break; }
        void* rbuf = SysUserPtr(frame->rcx);
        if (!rbuf) { ret = (u64)-1; break; }
        u32 rcount = (u32)frame->rdx;

        KFileDesc* f = &g_fd_table[fd];
        u32 remaining = f->size - f->pos;
        if (rcount > remaining) rcount = remaining;

        if (rcount > 0) {
            u32 bytes_read = 0;
            KThread* t = PsGetCurrentThread();
            u64 saved_cr3 = __readcr3();
            if (t && t->process) __writecr3(t->process->page_table);
            ntstatus status = Fat32ReadFileAt(f->path, f->pos, rbuf, rcount, &bytes_read);
            if (t && t->process) __writecr3(saved_cr3);

            KdPrintf("[DEBUG SYS_READ] fd=%d path='%s' pos=%u rcount=%u bytes_read=%u status=0x%x\n",
                     fd, f->path, f->pos, rcount, bytes_read, status);
            if (bytes_read > 0) {
                u8* p_user = (u8*)rbuf;
                KdPrintf("[DEBUG SYS_READ] user first bytes: 0x%02x 0x%02x 0x%02x 0x%02x\n",
                         p_user[0], p_user[1], p_user[2], p_user[3]);
            }

            if (NT_ERROR(status)) { ret = (u64)-1; break; }
            f->pos += bytes_read;
            ret = (u64)bytes_read;
        } else {
            ret = 0;
        }
        break;
    }
    case SYS_LSEEK: {
        i32 fd = (i32)frame->rbx;
        if (fd < 0 || fd >= MAX_FD || !g_fd_table[fd].used) { ret = (u64)-1; break; }
        i64 offset = (i64)frame->rcx;
        i32 whence = (i32)frame->rdx;

        KFileDesc* f = &g_fd_table[fd];
        i64 newpos = 0;
        if (whence == 0) {
            newpos = offset;
        } else if (whence == 1) {
            newpos = (i64)f->pos + offset;
        } else if (whence == 2) {
            newpos = (i64)f->size + offset;
        }
        if (newpos < 0) newpos = 0;
        if (newpos > (i64)f->size) newpos = (i64)f->size;
        f->pos = (u32)newpos;
        ret = (u64)f->pos;
        break;
    }
    case SYS_CLOSEFD: {
        i32 fd = (i32)frame->rbx;
        if (fd < 0 || fd >= MAX_FD || !g_fd_table[fd].used) { ret = (u64)-1; break; }
        if (g_fd_table[fd].path) KmFree(g_fd_table[fd].path);
        g_fd_table[fd].path = NULL;
        g_fd_table[fd].size = 0;
        g_fd_table[fd].pos = 0;
        g_fd_table[fd].used = 0;
        ret = 0;
        break;
    }
    case SYS_AUDIO_INIT: {
        extern void AudioInit(void);
        extern i32 AudioInitialized(void);
        AudioInit();
        ret = AudioInitialized() ? 0 : (u64)-1;
        break;
    }
    case SYS_AUDIO_PLAY: {
        extern ntstatus AudioPlay(const void* buffer, u32 size);
        void* buf = SysUserPtr(frame->rbx);
        u32 size = (u32)frame->rcx;
        if (!buf || size == 0) { ret = (u64)-1; break; }

        KThread* t = PsGetCurrentThread();
        if (t && t->process) {
            u64* pml4 = (u64*)PHYS_TO_VIRT(t->process->page_table);
            u64 start_virt = ((u64)(usize)buf) & ~0xFFFULL;
            u64 end_virt = ((u64)(usize)buf + size + PAGE_SIZE - 1) & ~0xFFFULL;
            i32 valid = 1;
            for (u64 v = start_virt; v < end_virt; v += PAGE_SIZE) {
                if (!VmmGetPhysical(pml4, v)) {
                    valid = 0; break;
                }
            }
            if (!valid) {
                KdPrintf("[SYSCALL] AUDIO_PLAY: Invalid user buffer 0x%llx size %u\n", (u64)(usize)buf, size);
                ret = (u64)-1; break;
            }
        }

        ntstatus astatus = AudioPlay(buf, size);
        ret = NT_SUCCESS(astatus) ? 0 : (u64)-1;
        break;
    }
    case SYS_AUDIO_STOP: {
        extern void AudioStop(void);
        AudioStop();
        ret = 0;
        break;
    }
    case SYS_WAITPID: {
        u64 wait_pid = frame->rbx;
        KdPrintf("[SYSCALL] WAITPID: wait_pid=%llu\n", wait_pid);
        KProcess* wp = PsFindProcess(wait_pid);
        if (!wp || wp->state == PROCESS_TERMINATED) {
            if (wp) {
                KdPrintf("[SYSCALL] WAITPID: PID=%llu already terminated\n", wait_pid);
                PsCleanupTerminated();
            }
            ret = 0;
            break;
        }
        KThread* wt = PsGetCurrentThread();
        if (!wt || !wt->process) { ret = (u64)-1; break; }

        KThread* next = PsScheduleNext();
        KdPrintf("[SYSCALL] WAITPID: next=%p has_saved=%d\n", next, next ? next->has_saved_frame : -1);
        if (!next || !next->has_saved_frame) {
            KdPrintf("[SYSCALL] WAITPID: blocking, no next thread\n");
            wt->state = PROCESS_BLOCKED;
            wt->wakeup_tick = 0;
            wt->wait_pid = wait_pid;
            RtMemCopy(&wt->saved_frame, frame, sizeof(TrapFrame));
            wt->has_saved_frame = 1;

            while (1) {
                KProcess* chk = PsFindProcess(wait_pid);
                if (!chk || chk->state == PROCESS_TERMINATED) {
                    PsCleanupTerminated();
                    RtMemCopy(frame, &wt->saved_frame, sizeof(TrapFrame));
                    wt->has_saved_frame = 0;
                    wt->state = PROCESS_READY;
                    wt->wait_pid = 0;
                    break;
                }
                HalHlt();
                next = PsScheduleNext();
                if (next && next->has_saved_frame) {
                    KdPrintf("[SYSCALL] WAITPID: switching to TID=%llu PID=%llu from busywait\n",
                             next->tid, next->process ? next->process->pid : 0);
                    RtMemCopy(frame, &next->saved_frame, sizeof(TrapFrame));
                    next->has_saved_frame = 0;
                    PsSetCurrentThread(next);
                    if (next->process) {
                        KeSetTssRsp0(next->kernel_stack_top);
                        __writecr3(next->context.cr3);
                    }
                    return;
                }
            }
            ret = 0;
            break;
        }

        KdPrintf("[SYSCALL] WAITPID: switching to TID=%llu PID=%llu RIP=0x%llx CS=0x%llx\n",
                 next->tid, next->process ? next->process->pid : 0,
                 next->saved_frame.rip, next->saved_frame.cs);
        wt->state = PROCESS_BLOCKED;
        wt->wakeup_tick = 0;
        wt->wait_pid = wait_pid;

        RtMemCopy(&wt->saved_frame, frame, sizeof(TrapFrame));
        wt->has_saved_frame = 1;

        RtMemCopy(frame, &next->saved_frame, sizeof(TrapFrame));
        next->has_saved_frame = 0;
        PsSetCurrentThread(next);
        if (next->process) {
            KeSetTssRsp0(next->kernel_stack_top);
            __writecr3(next->context.cr3);
        }
        return;
    }
    case SYS_NET_OPEN: {
        extern i32 TcpSocketCreate(void);
        ret = (u64)TcpSocketCreate();
        break;
    }
    case SYS_NET_CONNECT: {
        extern ntstatus TcpConnect(i32 sock, u32 dst_ip, u16 dst_port);
        i32 sock = (i32)frame->rbx;
        u32 ip = (u32)frame->rcx;
        u16 port = (u16)frame->rdx;
        ntstatus st = TcpConnect(sock, ip, port);
        ret = NT_SUCCESS(st) ? 0 : (u64)-1;
        break;
    }
    case SYS_NET_SEND: {
        extern i32 TcpSend(i32 sock, const void* data, u16 len);
        i32 sock = (i32)frame->rbx;
        void* buf = SysUserPtr(frame->rcx);
        u16 len = (u16)frame->rdx;
        if (!buf) { ret = (u64)-1; break; }
        ret = (u64)TcpSend(sock, buf, len);
        break;
    }
    case SYS_NET_RECV: {
        extern i32 TcpRecv(i32 sock, void* buf, u16 max_len, u32 timeout_ms);
        i32 sock = (i32)frame->rbx;
        void* buf = SysUserPtr(frame->rcx);
        u16 len = (u16)frame->rdx;
        u32 timeout = (u32)frame->rsi;
        if (!buf) { ret = (u64)-1; break; }
        ret = (u64)TcpRecv(sock, buf, len, timeout);
        break;
    }
    case SYS_NET_CLOSE: {
        extern ntstatus TcpClose(i32 sock);
        i32 sock = (i32)frame->rbx;
        ntstatus st = TcpClose(sock);
        ret = NT_SUCCESS(st) ? 0 : (u64)-1;
        break;
    }
    default:
        KdPrintf("[SYSCALL] Unknown syscall %llu\n", syscall_num);
        ret = (u64)-1;
        break;
    }

    frame->rax = ret;
}
