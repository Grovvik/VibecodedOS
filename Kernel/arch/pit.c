#include "pit.h"
#include "hal.h"
#include "debug.h"
#include "process.h"
#include "gdt.h"
#include "runtime.h"

extern KProcess* g_process_list;
extern void NetPoll(void);

#define PIT_CHANNEL0  0x40
#define PIT_CHANNEL1  0x41
#define PIT_CHANNEL2  0x42
#define PIT_COMMAND   0x43

#define PIT_BASE_FREQ 1193182

#define PIT_CMD_BINARY    0x00
#define PIT_CMD_MODE3     0x06
#define PIT_CMD_LATCH     0x00
#define PIT_CMD_LOBYTE    0x10
#define PIT_CMD_HIBYTE    0x20
#define PIT_CMD_CHANNEL0  0x00

static volatile u64 g_tick_count = 0;
static u32 g_pit_frequency = 0;

void KeInitPit(u32 freq) {
    KdPrintf("[PIT] Initializing 8254 PIT at %u Hz...\n", freq);

    g_pit_frequency = freq;
    u16 divisor = (u16)(PIT_BASE_FREQ / freq);
    if (divisor < 1) divisor = 1;
    if (divisor > 65535) divisor = 65535;

    KdPrintf("[PIT] Divisor = %u (actual freq = %u Hz)\n",
             divisor, PIT_BASE_FREQ / divisor);

    HalOutByte(PIT_COMMAND, PIT_CMD_CHANNEL0 | PIT_CMD_LOBYTE | PIT_CMD_HIBYTE | PIT_CMD_MODE3);
    HalIoWait();
    HalOutByte(PIT_CHANNEL0, (u8)(divisor & 0xFF));
    HalIoWait();
    HalOutByte(PIT_CHANNEL0, (u8)((divisor >> 8) & 0xFF));
    HalIoWait();

    KdPrintf("[PIT] PIT initialized OK\n");
}

void PitIrqHandler(TrapFrame* frame) {
    g_tick_count++;

    NetPoll();

    extern void HdaPoll(void);
    HdaPoll();

    KProcess* scan = g_process_list;
    while (scan) {
        if (scan->state != PROCESS_TERMINATED) {
            for (u32 i = 0; i < scan->thread_count && i < 16; i++) {
                KThread* t = scan->threads[i];
                if (t && t->state == PROCESS_BLOCKED) {
                    if (t->wakeup_tick > 0 && g_tick_count >= t->wakeup_tick) {
                        t->state = PROCESS_READY;
                        t->wakeup_tick = 0;
                    } else if (t->wait_pid != 0) {
                        KProcess* wp = PsFindProcess(t->wait_pid);
                        if (!wp || wp->state == PROCESS_TERMINATED) {
                            t->state = PROCESS_READY;
                            t->wait_pid = 0;
                        }
                    }
                }
            }
        }
        scan = scan->next;
    }

    KThread* current = PsGetCurrentThread();
    if (!current) return;
    if (current->process && current->process->state == PROCESS_TERMINATED) {
        return;
    }

    i32 was_user = (i32)(frame->cs & 3);

    if (!was_user) return;

    PsCleanupTerminated();

    KThread* next = PsScheduleNext();
    if (!next) return;

    u64 cur_pid = current->process ? current->process->pid : 0;
    u64 nxt_pid = next->process ? next->process->pid : 0;
    KdPrintf("[PIT] PREEMPT: PID=%llu -> PID=%llu RIP=0x%llx->0x%llx\n",
             cur_pid, nxt_pid, frame->rip, next->saved_frame.rip);

    if (current->state == PROCESS_READY) {
        RtMemCopy(&current->saved_frame, frame, sizeof(TrapFrame));
        current->has_saved_frame = 1;
    }

    if (next->has_saved_frame) {
        RtMemCopy(frame, &next->saved_frame, sizeof(TrapFrame));
        next->has_saved_frame = 0;
    }

    PsSetCurrentThread(next);
    if (next->process) {
        KeSetTssRsp0(next->kernel_stack_top);
    }
    __writecr3(next->context.cr3);
}

u64 KeGetTickCount(void) {
    return g_tick_count;
}

void KeSleepTicks(u64 ticks) {
    u64 target = g_tick_count + ticks;
    while (g_tick_count < target) {
        HalHlt();
    }
}
