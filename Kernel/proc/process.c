#include "process.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "debug.h"
#include "runtime.h"
#include "hal.h"
#include "boot_info.h"
#include "error.h"

extern u64* g_kernel_pml4;

KProcess* g_process_list;
static KProcess* g_current_process;
static KThread*  g_current_thread;
static u64       g_next_pid = 1;
static u64       g_next_tid = 1;

void PsInit(void) {
    g_process_list = NULL;
    g_current_process = NULL;
    g_current_thread = NULL;
    KdPrintf("[PS] Process subsystem initialized\n");
}

KProcess* PsCreateProcess(const char* name, u64 entry, u64 image_base, u64 image_size) {
    KProcess* proc = (KProcess*)KmAlloc(sizeof(KProcess));
    if (!proc) return NULL;
    RtMemSet(proc, 0, sizeof(KProcess));

    proc->pid = g_next_pid++;
    proc->state = PROCESS_READY;
    RtStrnCopy(proc->name, name, PROCESS_NAME_MAX - 1);
    proc->image_base = image_base;
    proc->image_size = image_size;
    proc->entry_point = entry;
    proc->heap_base = image_base + ((image_size + PAGE_SIZE - 1) & ~0xFFFULL);
    proc->heap_brk = proc->heap_base;

    u64* proc_pml4 = VmmCreateAddressSpace();
    if (!proc_pml4) {
        KmFree(proc);
        return NULL;
    }
    proc->page_table = (u64)(usize)proc_pml4 - PHYS_OFFSET;

    proc->prev = NULL;
    proc->next = g_process_list;
    if (g_process_list) g_process_list->prev = proc;
    g_process_list = proc;

    KdPrintf("[PS] Created process '%s' PID=%llu entry=0x%llx PML4=0x%llx\n",
             proc->name, proc->pid, entry, proc->page_table);
    return proc;
}

void PsDestroyProcess(KProcess* proc) {
    if (!proc) return;

    for (u32 i = 0; i < MAX_THREADS && i < proc->thread_count; i++) {
        if (proc->threads[i]) {
            PsDestroyThread(proc->threads[i]);
        }
    }

    if (proc->page_table && proc->page_table != HalReadCr3()) {
        u64* pml4 = (u64*)PHYS_TO_VIRT(proc->page_table);
        for (i32 i = 0; i < 256; i++) {
            if ((pml4[i] & VMM_PRESENT) && i >= 1 && i < 256) {
                u64* pdpt = (u64*)PHYS_TO_VIRT(pml4[i] & ~0xFFFULL);
                for (i32 j = 0; j < 512; j++) {
                    if (pdpt[j] & VMM_PRESENT) {
                        u64* pd = (u64*)PHYS_TO_VIRT(pdpt[j] & ~0xFFFULL);
                        for (i32 k = 0; k < 512; k++) {
                            if (pd[k] & VMM_PRESENT && !(pd[k] & (1ULL << 7))) {
                                u64* pt = (u64*)PHYS_TO_VIRT(pd[k] & ~0xFFFULL);
                                for (i32 l = 0; l < 512; l++) {
                                    if (pt[l] & VMM_PRESENT) {
                                        PmmFreePage(pt[l] & ~0xFFFULL);
                                    }
                                }
                                PmmFreePage((u64)(usize)pt);
                            }
                        }
                        PmmFreePage((u64)(usize)pd);
                    }
                }
                PmmFreePage((u64)(usize)pdpt);
            }
        }
        PmmFreePage((u64)(usize)pml4);
    }

    if (proc->prev) proc->prev->next = proc->next;
    else g_process_list = proc->next;
    if (proc->next) proc->next->prev = proc->prev;

    if (g_current_process == proc) {
        g_current_process = NULL;
        g_current_thread = NULL;
    }

    KmFree(proc);
}

KThread* PsCreateThread(KProcess* proc, u64 entry, u64 stack_top) {
    if (!proc) return NULL;
    if (proc->thread_count >= MAX_THREADS) return NULL;

    KThread* thread = (KThread*)KmAlloc(sizeof(KThread));
    if (!thread) return NULL;
    RtMemSet(thread, 0, sizeof(KThread));

    thread->tid = g_next_tid++;
    thread->process = proc;
    thread->state = PROCESS_READY;

    u64 stack_pages = USER_STACK_SIZE / PAGE_SIZE;
    u64 stack_phys = PmmAllocPages(stack_pages);
    if (!stack_phys) {
        KmFree(thread);
        return NULL;
    }

    u64* pml4 = (u64*)PHYS_TO_VIRT(proc->page_table);
    u64 stack_virt = 0x50000000ULL;
    VmmMapPages(pml4, stack_virt, stack_phys, stack_pages, VMM_USER_FLAGS | VMM_NX);

    thread->stack_top = stack_virt + USER_STACK_SIZE;
    thread->stack_base = stack_virt;

    u64 kstack_phys = PmmAllocPages(16);
    if (!kstack_phys) {
        KmFree(thread);
        return NULL;
    }
    KdPrintf("[PS] Kernel stack: phys=0x%llx - 0x%llx (TID=%llu PID=%llu)\n",
             kstack_phys, kstack_phys + PAGE_SIZE * 16, thread->tid, proc->pid);
    thread->kernel_stack_top = (u64)PHYS_TO_VIRT(kstack_phys + PAGE_SIZE * 16);
    thread->kernel_stack_base = (u64)PHYS_TO_VIRT(kstack_phys);

    RtMemSet(&thread->context, 0, sizeof(ThreadContext));
    thread->context.rip = entry;
    thread->context.rsp = thread->stack_top - 4096;
    thread->context.rflags = 0x202;
    thread->context.cr3 = proc->page_table;

    u32 idx = proc->thread_count;
    proc->threads[idx] = thread;
    proc->thread_count++;

    KdPrintf("[PS] Created thread TID=%llu in PID=%llu entry=0x%llx\n",
             thread->tid, proc->pid, entry);
    return thread;
}

void PsDestroyThread(KThread* thread) {
    if (!thread) return;
    KmFree(thread);
}

KProcess* PsGetCurrentProcess(void) { return g_current_process; }
KThread*  PsGetCurrentThread(void) { return g_current_thread; }
void      PsSetCurrentThread(KThread* thread) {
    g_current_thread = thread;
    if (thread) g_current_process = thread->process;
}

KProcess* PsFindProcess(u64 pid) {
    KProcess* p = g_process_list;
    while (p) {
        if (p->pid == pid) return p;
        p = p->next;
    }
    return NULL;
}

void PsDumpProcessList(void) {
    KProcess* p = g_process_list;
    KdPrintf("[PS] Process list:\n");
    while (p) {
        KdPrintf("[PS]   PID=%llu name='%s' state=%d threads=%u PML4=0x%llx\n",
                 p->pid, p->name, p->state, p->thread_count, p->page_table);
        p = p->next;
    }
}

KThread* PsScheduleNext(void) {
    KProcess* p = g_process_list;
    KThread* current = g_current_thread;

    while (p) {
        if (p->state != PROCESS_TERMINATED && p->thread_count > 0) {
            KThread* t = p->threads[0];
            if (t && t->state == PROCESS_READY && t != current && t->has_saved_frame) {
                return t;
            }
        }
        p = p->next;
    }

    return NULL;
}

static void PsFreeAddressSpace(u64 page_table) {
    if (!page_table || page_table == HalReadCr3()) return;

    u64* pml4 = (u64*)PHYS_TO_VIRT(page_table);

    for (i32 i = 0; i < 256; i++) {
        if (!(pml4[i] & VMM_PRESENT)) continue;
        u64 pdpt_phys = pml4[i] & ~0xFFFULL;
        u64* pdpt = (u64*)PHYS_TO_VIRT(pdpt_phys);

        for (i32 j = 0; j < 512; j++) {
            if (!(pdpt[j] & VMM_PRESENT)) continue;
            if (pdpt[j] & VMM_HUGE) continue;
            u64 pd_phys = pdpt[j] & ~0xFFFULL;
            u64* pd = (u64*)PHYS_TO_VIRT(pd_phys);

            for (i32 k = 0; k < 512; k++) {
                if (!(pd[k] & VMM_PRESENT)) continue;
                if (pd[k] & VMM_HUGE) continue;
                u64 pt_phys = pd[k] & ~0xFFFULL;
                u64* pt = (u64*)PHYS_TO_VIRT(pt_phys);

                for (i32 l = 0; l < 512; l++) {
                    if (!(pt[l] & VMM_PRESENT)) continue;
                    u64 virt = ((u64)i << 39) | ((u64)j << 30) | ((u64)k << 21) | ((u64)l << 12);
                    u64 phys = pt[l] & ~0xFFFULL;
                    if (virt != phys && PmmIsPageTracked(phys)) {
                        PmmFreePage(phys);
                    }
                }
                if (PmmIsPageTracked(pt_phys)) {
                    PmmFreePage(pt_phys);
                }
            }
            if (PmmIsPageTracked(pd_phys)) {
                PmmFreePage(pd_phys);
            }
        }
         /* Determine if PDPT contains kernel mappings (indices 256-511). If so, skip freeing. */
         int pdpt_has_kernel = 0;
         for (i32 kk = 256; kk < 512; kk++) {
             if (pdpt[kk] & VMM_PRESENT) {
                 pdpt_has_kernel = 1;
                 break;
             }
         }
         if (!pdpt_has_kernel) {
             if (PmmIsPageTracked(pdpt_phys)) {
                //  KdPrintf("[PsFree] Freeing PDPT page: phys=0x%llx\n", pdpt_phys);
                 PmmFreePage(pdpt_phys);
             }
         } else {
             KdPrintf("[PsFree] Skipping PDPT free due to kernel mappings: phys=0x%llx\n", pdpt_phys);
         }
    }
    if (PmmIsPageTracked(page_table)) {
        // KdPrintf("[PsFree] Freeing PML4 page: phys=0x%llx\n", page_table);
        PmmFreePage(page_table);
    }
}

void PsCleanupTerminated(void) {
    KProcess** pp = &g_process_list;
    while (*pp) {
        KProcess* p = *pp;
        if (p->state == PROCESS_TERMINATED) {
            KProcess* next = p->next;
            *pp = next;
            if (next) next->prev = p->prev;

            for (u32 i = 0; i < p->thread_count && i < MAX_THREADS; i++) {
                if (p->threads[i]) {
                    KThread* t = p->threads[i];
                    if (g_current_thread == t) {
                        g_current_thread = NULL;
                        g_current_process = NULL;
                    }
                    if (t->kernel_stack_base) {
                        PmmFreePages((u64)(t->kernel_stack_base - PHYS_OFFSET), 16);
                    }
                    KmFree(t);
                }
            }
            PsFreeAddressSpace(p->page_table);
            if (g_current_process == p) {
                g_current_process = NULL;
                g_current_thread = NULL;
            }
            KmFree(p);
        } else {
            pp = &p->next;
        }
    }
}
