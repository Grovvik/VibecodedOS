#ifndef _KERNEL_PROC_PROCESS_H_
#define _KERNEL_PROC_PROCESS_H_

#include "types.h"
#include "cpu.h"

#define PROCESS_NAME_MAX 32
#define MAX_THREADS 16
#define MAX_OPEN_FILES 32
#define USER_STACK_SIZE (512 * 1024)
#define USER_CODE_BASE  0x400000ULL

typedef enum {
    PROCESS_RUNNING,
    PROCESS_READY,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED,
    PROCESS_ZOMBIE
} ProcessState;

typedef struct KThread KThread;
typedef struct KProcess KProcess;

struct KThread {
    u64          tid;
    KProcess*    process;
    ProcessState state;
    u64          stack_top;
    u64          stack_base;
    u64          kernel_stack_top;
    u64          kernel_stack_base;
    ThreadContext context;
    u64          wakeup_tick;
    TrapFrame    saved_frame;
    i32          has_saved_frame;
    u64          wait_pid;
    KThread*     next;
    KThread*     prev;
};

struct KProcess {
    u64          pid;
    ProcessState state;
    char         name[PROCESS_NAME_MAX];
    u64          page_table;
    u64          image_base;
    u64          image_size;
    u64          entry_point;
    u64          heap_brk;
    u64          heap_base;
    KThread*     threads[MAX_THREADS];
    u32          thread_count;
    KProcess*    next;
    KProcess*    prev;
};

void PsInit(void);

KProcess* PsCreateProcess(const char* name, u64 entry, u64 image_base, u64 image_size);
void      PsDestroyProcess(KProcess* proc);

KThread*  PsCreateThread(KProcess* proc, u64 entry, u64 stack_top);
void      PsDestroyThread(KThread* thread);

KProcess* PsGetCurrentProcess(void);
KThread*  PsGetCurrentThread(void);

void      PsSetCurrentThread(KThread* thread);

KProcess* PsFindProcess(u64 pid);

KThread*  PsScheduleNext(void);
void      PsCleanupTerminated(void);

void      PsDumpProcessList(void);

#endif