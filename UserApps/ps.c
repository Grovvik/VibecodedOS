#include "userlib.h"

void main(const char* args, const char* cwd, i32 argc) {
    PsInfoEntry entries[PS_INFO_MAX];
    memset(entries, 0, sizeof(entries));
    u64 count = syscall3(SYS_SYSTEM, (u64)SYS_SYS_PS, (u64)(usize)entries, (u64)PS_INFO_MAX);
    if (!count) { print("No processes\n"); return; }

    setcolor(FB_CYAN, FB_BLACK);
    print("Process list:\n");
    setcolor(FB_WHITE, FB_BLACK);

    for (u32 i = 0; i < (u32)count; i++) {
        const char* state_str = "???";
        switch (entries[i].state) {
        case 0: state_str = "RUNNING"; break;
        case 1: state_str = "READY"; break;
        case 2: state_str = "BLOCKED"; break;
        case 3: state_str = "TERMINATED"; break;
        case 4: state_str = "ZOMBIE"; break;
        }
        printf("  PID=%u name='%s' state=%s threads=%u\n",
               (u32)entries[i].pid, entries[i].name, state_str, entries[i].thread_count);
    }
}
