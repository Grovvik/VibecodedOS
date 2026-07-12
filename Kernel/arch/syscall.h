#ifndef _KERNEL_ARCH_SYSCALL_H_
#define _KERNEL_ARCH_SYSCALL_H_

#include "types.h"
#include "cpu.h"
#include "keyboard.h"

#define SYSCALL_INT 0x80

#define SYS_WRITE       1
#define SYS_EXIT        2
#define SYS_GETPID      3
#define SYS_MMAP        4
#define SYS_PUTS       10
#define SYS_GETCHAR    11
#define SYS_CLEAR      12
#define SYS_SYSTEM     22
#define SYS_SETCOLOR   23
#define SYS_GETCWD     27
#define SYS_FS_OPENDIR  28
#define SYS_FS_READDIR  29
#define SYS_FS_CLOSEDIR 30
#define SYS_FS_OPENFILE 31
#define SYS_FS_READFILE 32
#define SYS_FS_FILESIZE 33
#define SYS_FS_CLOSEFILE  34
#define SYS_FS_WRITEFILE  35
#define SYS_FS_DELETE     36
#define SYS_FS_MKDIR      37
#define SYS_EXEC          38
#define SYS_SLEEP         39
#define SYS_CHDIR         40
#define SYS_PUTCHAR       41
#define SYS_WAITPID       42
#define SYS_FB_MAP        43
#define SYS_GETKEY        44
#define SYS_OPEN          45
#define SYS_READ          46
#define SYS_LSEEK         47
#define SYS_CLOSEFD       48

#define SYS_AUDIO_INIT    49
#define SYS_AUDIO_PLAY    50
#define SYS_AUDIO_STOP    51

#define SYS_NET_OPEN      52
#define SYS_NET_CONNECT   53
#define SYS_NET_SEND      54
#define SYS_NET_RECV      55
#define SYS_NET_CLOSE     56
#define SYS_DNS_RESOLVE   57

#define SYS_SYS_REBOOT      0
#define SYS_SYS_HALT        1
#define SYS_SYS_TICKS       2
#define SYS_SYS_MEMINFO     3
#define SYS_SYS_PS          4
#define SYS_SYS_MEMUSED     5
#define SYS_SYS_DISKSIZE    6
#define SYS_SYS_IP          7

#define PS_INFO_MAX         32

typedef struct {
    char name[64];
    u64  pid;
    i32  state;
    u32  thread_count;
} PsInfoEntry;

typedef struct {
    u64 fb_virt;
    u32 width;
    u32 height;
    u32 pitch;
    u32 bpp;
} FbInfo;

#define MAX_FD 16

typedef struct {
    char* path;
    u32 size;
    u32 pos;
    i32 used;
} KFileDesc;

void SyscallInit(void);
void SyscallSetExitReturn(u64 rip, u64 rsp);
void SyscallKillCurrentProcess(TrapFrame* frame);
void SyscallIsrHandler(void* frame);
void SysResolvePath(const char* user_path, char* resolved);

#endif