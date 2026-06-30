#include "doomgeneric.h"
#include "doomkeys.h"
#include "micront_crt.h"

#ifdef FEATURE_SOUND
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "m_misc.h"
#endif

typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef long long i64;
typedef int i32;

#define PAGE_SIZE 4096

#define SYS_FB_MAP    43
#define SYS_GETKEY    44
#define SYS_OPEN      45
#define SYS_READ      46
#define SYS_LSEEK     47
#define SYS_CLOSEFD   48
#define SYS_PUTS     10
#define SYS_PUTCHAR  41
#define SYS_SLEEP    39
#define SYS_SYSTEM   22
#define SYS_EXIT      2
#define SYS_MMAP      4

#define SYS_AUDIO_INIT  49
#define SYS_AUDIO_PLAY  50
#define SYS_AUDIO_STOP  51

u64 syscall0(u64 num);
u64 syscall1(u64 num, u64 a1);
u64 syscall2(u64 num, u64 a1, u64 a2);
u64 syscall3(u64 num, u64 a1, u64 a2, u64 a3);

#pragma pack(push, 1)
typedef struct {
    u64 fb_virt;
    u32 width;
    u32 height;
    u32 pitch;
    u32 bpp;
} FbInfo;

typedef struct {
    u32 scancode;
    u32 pressed;
} KeyEvent;
#pragma pack(pop)

static FbInfo g_fb_info;
static u32* g_framebuffer = NULL;

#define KEYQUEUE_SIZE 64
static unsigned short g_key_queue[KEYQUEUE_SIZE];
static unsigned int g_key_queue_write = 0;
static unsigned int g_key_queue_read = 0;

static unsigned char scancode_to_doom_key(u32 scancode) {
    switch (scancode) {
    case 0x01: return KEY_ESCAPE;
    case 0x1C: case 0x9C: return KEY_ENTER;
    case 0x48: case 0x148: return KEY_UPARROW;
    case 0x50: case 0x150: return KEY_DOWNARROW;
    case 0x4B: case 0x14B: return KEY_LEFTARROW;
    case 0x4D: case 0x14D: return KEY_RIGHTARROW;
    case 0x1D: case 0x11D: return KEY_FIRE;
    case 0x39: return KEY_USE;
    case 0x2A: case 0x36: return KEY_RSHIFT;
    case 0x38: case 0x138: return KEY_RALT;
    case 0x0F: return KEY_TAB;
    case 0x3A: return KEY_CAPSLOCK;
    case 0x0E: return KEY_BACKSPACE;
    case 0x3B: return KEY_F1;
    case 0x3C: return KEY_F2;
    case 0x3D: return KEY_F3;
    case 0x3E: return KEY_F4;
    case 0x3F: return KEY_F5;
    case 0x40: return KEY_F6;
    case 0x41: return KEY_F7;
    case 0x42: return KEY_F8;
    case 0x43: return KEY_F9;
    case 0x44: return KEY_F10;
    case 0x57: return KEY_F11;
    case 0x58: return KEY_F12;
    case 0x152: return KEY_INS;
    case 0x153: return KEY_DEL;
    case 0x147: return KEY_HOME;
    case 0x14F: return KEY_END;
    case 0x149: return KEY_PGUP;
    case 0x151: return KEY_PGDN;
    default:
        if (scancode < 0x80) {
            static const char sc_to_ascii[] = {
                0,27,'1','2','3','4','5','6','7','8','9','0','-','=',0,
                0,'q','w','e','r','t','y','u','i','o','p','[',']',0,
                0,'a','s','d','f','g','h','j','k','l',';','\'','`',
                0,'\\','z','x','c','v','b','n','m',',','.','/',0,
                '*',0,' '
            };
            if (scancode < sizeof(sc_to_ascii))
                return (unsigned char)sc_to_ascii[scancode];
        }
        return 0;
    }
}

static void add_key_to_queue(int pressed, u32 scancode) {
    unsigned char doom_key = scancode_to_doom_key(scancode);
    if (doom_key == 0) return;
    unsigned short key_data = (unsigned short)((pressed << 8) | doom_key);
    g_key_queue[g_key_queue_write] = key_data;
    g_key_queue_write = (g_key_queue_write + 1) % KEYQUEUE_SIZE;
}

static void handle_key_input(void) {
    KeyEvent ev;
    while (1) {
        i32 got = (i32)syscall1(SYS_GETKEY, (u64)(size_t)&ev);
        if (!got) break;
        add_key_to_queue(ev.pressed ? 1 : 0, ev.scancode);
    }
}

#ifdef FEATURE_SOUND
static void DG_InitSoundModule(void);
#endif

void DG_Init(void) {
    syscall1(SYS_PUTS, (u64)(size_t)"DOOM: DG_Init called\n");

    g_fb_info.fb_virt = 0;
    g_fb_info.width = 0;
    g_fb_info.height = 0;
    g_fb_info.pitch = 0;
    g_fb_info.bpp = 0;

    syscall1(SYS_PUTS, (u64)(size_t)"DOOM: calling SYS_FB_MAP\n");
    i32 rc = (i32)syscall1(SYS_FB_MAP, (u64)(size_t)&g_fb_info);
    if (rc != 0 || g_fb_info.fb_virt == 0) {
        syscall1(SYS_PUTS, (u64)(size_t)"DOOM: FB_MAP failed!\n");
        syscall1(SYS_EXIT, 1);
        for (;;) ;
    }

    g_framebuffer = (u32*)(size_t)g_fb_info.fb_virt;

    syscall1(SYS_PUTS, (u64)(size_t)"DOOM: Framebuffer mapped OK\n");

#ifdef FEATURE_SOUND
    DG_InitSoundModule();
    syscall1(SYS_PUTS, (u64)(size_t)"DOOM: Sound module initialized\n");
#endif
}

void DG_DrawFrame(void) {
    if (!g_framebuffer || !DG_ScreenBuffer) return;

    u32 fb_width = g_fb_info.width;
    u32 fb_height = g_fb_info.height;
    u32 fb_pitch = g_fb_info.pitch;
    u32 doompitch = DOOMGENERIC_RESX * 4;

    u32 copy_w = DOOMGENERIC_RESX < fb_width ? DOOMGENERIC_RESX : fb_width;
    u32 copy_h = DOOMGENERIC_RESY < fb_height ? DOOMGENERIC_RESY : fb_height;

    u32 x_offset = 0;
    u32 y_offset = 0;

    for (u32 y = 0; y < copy_h; y++) {
        u8* dst = (u8*)g_framebuffer + (y + y_offset) * fb_pitch + x_offset * 4;
        u8* src = (u8*)DG_ScreenBuffer + y * doompitch;
        memcpy(dst, src, copy_w * 4);
    }

    handle_key_input();
}

void DG_SleepMs(uint32_t ms) {
    syscall1(SYS_SLEEP, (u64)ms);
}

uint32_t DG_GetTicksMs(void) {
    u64 ticks = syscall2(SYS_SYSTEM, 2, 0);
    return (uint32_t)(ticks * 10);
}

int DG_GetKey(int* pressed, unsigned char* key) {
    if (g_key_queue_read == g_key_queue_write) return 0;

    unsigned short key_data = g_key_queue[g_key_queue_read];
    g_key_queue_read = (g_key_queue_read + 1) % KEYQUEUE_SIZE;

    *pressed = (key_data >> 8) & 0xFF;
    *key = key_data & 0xFF;

    return 1;
}

void DG_SetWindowTitle(const char* title) {
}

extern int myargc;
extern char** myargv;
extern void doomgeneric_Create(int argc, char** argv);
extern void doomgeneric_Tick(void);

static char g_argv_buf[512];
static char* g_argv_ptrs[16];

static int parse_args_to_argv(const char* args, char** argv, int max) {
    int argc = 0;
    char* p = g_argv_buf;
    int len = 0;
    while (args && *args && len < 500) { *p++ = *args++; len++; }
    *p = 0;

    p = g_argv_buf;
    while (*p && argc < max - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    argv[argc] = NULL;
    return argc;
}

extern int ticdup;

#ifdef FEATURE_SOUND

static i32 audio_init(void) {
    return (i32)syscall0(SYS_AUDIO_INIT);
}

static i32 audio_play(const void* buf, u32 size) {
    return (i32)syscall2(SYS_AUDIO_PLAY, (u64)(size_t)buf, (u64)size);
}

static void audio_stop(void) {
    syscall0(SYS_AUDIO_STOP);
}

/* MicroNT HDA sound module for DOOM */

#define DOOM_OUTPUT_RATE 48000
#define MAX_SND_CHANNELS 8

typedef struct {
    void* data;
    u32 size;
    u32 duration_ms;
} MntSfxCache;

static boolean mnt_sound_inited = false;
static boolean mnt_use_sfx_prefix = false;

static snddevice_t mnt_sound_devices[] = { SNDDEVICE_SB };

static boolean MntSoundInit(boolean use_sfx_prefix) {
    (void)use_sfx_prefix;
    mnt_use_sfx_prefix = use_sfx_prefix;
    if (audio_init() != 0) {
        syscall1(SYS_PUTS, (u64)(size_t)"DOOM SND: audio_init failed\n");
        return false;
    }
    mnt_sound_inited = true;
    syscall1(SYS_PUTS, (u64)(size_t)"DOOM SND: audio initialized\n");
    return true;
}

static void MntSoundShutdown(void) {
    audio_stop();
    mnt_sound_inited = false;
}

static int MntGetSfxLumpNum(sfxinfo_t *sfxinfo) {
    char namebuf[16];
    if (mnt_use_sfx_prefix) {
        snprintf(namebuf, sizeof(namebuf), "%.2s%s", "DS", sfxinfo->name);
    } else {
        snprintf(namebuf, sizeof(namebuf), "%s", sfxinfo->name);
    }
    return W_CheckNumForName(namebuf);
}

static void* MntConvertSound(byte *data, int samplerate, int length, u32* out_size, u32* out_duration_ms) {
    if (samplerate <= 0 || length <= 0) return NULL;

    /* Calculate output length at 48kHz stereo 16-bit */
    u32 out_samples = (u32)(((u64)length * DOOM_OUTPUT_RATE + samplerate - 1) / samplerate);
    u32 out_bytes = out_samples * 2 * 2; /* stereo * 16-bit */
    if (out_bytes == 0 || out_bytes / 4 != out_samples) return NULL; /* overflow or zero */

    short* out_buf = (short*)malloc(out_bytes);
    if (!out_buf) return NULL;

    /* Nearest-neighbor resample */
    u64 pos = 0;
    u64 step = ((u64)length << 16) / out_samples;
    for (u32 i = 0; i < out_samples; i++) {
        u32 src_idx = (u32)(pos >> 16);
        if (src_idx >= (u32)length) src_idx = (u32)length - 1;
        int sample = (int)data[src_idx] - 128;
        sample = (sample * 256);
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        out_buf[i * 2] = (short)sample;
        out_buf[i * 2 + 1] = (short)sample;
        pos += step;
    }

    *out_size = out_bytes;
    *out_duration_ms = (out_samples * 1000 + DOOM_OUTPUT_RATE - 1) / DOOM_OUTPUT_RATE;
    return out_buf;
}

static void MntCacheSounds(sfxinfo_t *sounds, int num_sounds) {
    int i;
    for (i = 0; i < num_sounds; i++) {
        sfxinfo_t *sfx = &sounds[i];
        if (sfx->driver_data) continue;

        // Check links
        sfxinfo_t *actual_sfx = sfx;
        if (actual_sfx->link != NULL) {
            actual_sfx = actual_sfx->link;
        }

        int lumpnum = W_CheckNumForName(actual_sfx->name);
        if (lumpnum < 0) {
            // Try prefix
            char prefix_name[16];
            snprintf(prefix_name, sizeof(prefix_name), "ds%s", actual_sfx->name);
            lumpnum = W_CheckNumForName(prefix_name);
        }

        if (lumpnum < 0) {
            continue;
        }

        sfx->lumpnum = lumpnum;

        int lump_len = W_LumpLength(lumpnum);
        if (lump_len < 8) {
            continue;
        }

        byte *data = W_CacheLumpNum(lumpnum, PU_STATIC);
        if (!data) {
            continue;
        }

        int format = (data[1] << 8) | data[0];
        int samplerate = (data[3] << 8) | data[2];
        int length = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];

        if (format != 3) {
            W_ReleaseLumpNum(lumpnum);
            continue;
        }

        if (length > lump_len - 8 || length <= 48) {
            W_ReleaseLumpNum(lumpnum);
            continue;
        }

        data += 16;
        length -= 32;

        u32 out_size = 0, out_duration = 0;
        void* converted = MntConvertSound(data + 8, samplerate, length, &out_size, &out_duration);

        /* Release the WAD lump */
        W_ReleaseLumpNum(lumpnum);

        if (converted) {
            MntSfxCache* cache = (MntSfxCache*)malloc(sizeof(MntSfxCache));
            if (cache) {
                cache->data = converted;
                cache->size = out_size;
                cache->duration_ms = out_duration;
                sfx->driver_data = cache;
            } else {
                free(converted);
            }
        }
    }
}

static void MntSoundUpdate(void) {
}

static void MntUpdateSoundParams(int channel, int vol, int sep) {
    (void)channel; (void)vol; (void)sep;
}

static int MntStartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep) {
    (void)channel; (void)vol; (void)sep;
    if (!mnt_sound_inited || !sfxinfo) return -1;
    if (!sfxinfo->driver_data) return -1;

    MntSfxCache* cache = (MntSfxCache*)sfxinfo->driver_data;
    if (!cache->data || cache->size == 0) return -1;

    audio_play(cache->data, cache->size);

    return channel;
}

static void MntStopSound(int channel) {
    (void)channel;
    audio_stop();
}

static boolean MntSoundIsPlaying(int channel) {
    (void)channel;
    return false;
}

/* Avoid static initializers with function pointers: /merge:.rdata=.text
 * causes PE loader to skip relocations in .text, leaving raw offsets.
 * Initialize these structs at runtime instead. */

/* Dummy music module functions */
static boolean MntMusicInit(void) { return false; }
static void MntMusicShutdown(void) {}
static void MntMusicSetVolume(int v) { (void)v; }
static void MntMusicPause(void) {}
static void MntMusicResume(void) {}
static void* MntMusicRegisterSong(void *data, int len) { (void)data; (void)len; return NULL; }
static void MntMusicUnRegisterSong(void *handle) { (void)handle; }
static void MntMusicPlaySong(void *handle, boolean looping) { (void)handle; (void)looping; }
static void MntMusicStopSong(void) {}
static boolean MntMusicIsPlaying(void) { return false; }
static void MntMusicPoll(void) {}

sound_module_t DG_sound_module;
music_module_t DG_music_module;

static void DG_InitSoundModule(void) {
    DG_sound_module.sound_devices = mnt_sound_devices;
    DG_sound_module.num_sound_devices = 1;
    DG_sound_module.Init = MntSoundInit;
    DG_sound_module.Shutdown = MntSoundShutdown;
    DG_sound_module.GetSfxLumpNum = MntGetSfxLumpNum;
    DG_sound_module.Update = MntSoundUpdate;
    DG_sound_module.UpdateSoundParams = MntUpdateSoundParams;
    DG_sound_module.StartSound = MntStartSound;
    DG_sound_module.StopSound = MntStopSound;
    DG_sound_module.SoundIsPlaying = MntSoundIsPlaying;
    DG_sound_module.CacheSounds = MntCacheSounds;

    DG_music_module.sound_devices = NULL;
    DG_music_module.num_sound_devices = 0;
    DG_music_module.Init = MntMusicInit;
    DG_music_module.Shutdown = MntMusicShutdown;
    DG_music_module.SetMusicVolume = MntMusicSetVolume;
    DG_music_module.PauseMusic = MntMusicPause;
    DG_music_module.ResumeMusic = MntMusicResume;
    DG_music_module.RegisterSong = MntMusicRegisterSong;
    DG_music_module.UnRegisterSong = MntMusicUnRegisterSong;
    DG_music_module.PlaySong = MntMusicPlaySong;
    DG_music_module.StopSong = MntMusicStopSong;
    DG_music_module.MusicIsPlaying = MntMusicIsPlaying;
    DG_music_module.Poll = MntMusicPoll;
}

#endif /* FEATURE_SOUND */

void main(const char* args, const char* cwd, i32 unused) {
    syscall1(SYS_PUTS, (u64)(size_t)"DOOM: Starting...\n");

    ticdup = 1;
    syscall1(SYS_PUTS, (u64)(size_t)"DOOM: ticdup set\n");

    g_argv_ptrs[0] = (char*)"doom";
    myargc = 1;
    myargv = g_argv_ptrs;

    if (args && *args) {
        myargc = parse_args_to_argv(args, g_argv_ptrs + 1, 15);
        myargc++;
        g_argv_ptrs[0] = (char*)"doom";
        myargv = g_argv_ptrs;
    }

    int has_iwad = 0;
    for (int i = 1; i < myargc; i++) {
        if (myargv[i] && strcmp(myargv[i], "-iwad") == 0) {
            has_iwad = 1;
            break;
        }
    }
    if (!has_iwad && myargc < 15) {
        g_argv_ptrs[myargc++] = (char*)"-iwad";
        g_argv_ptrs[myargc++] = (char*)"/bin/doom1.wad";
        g_argv_ptrs[myargc] = NULL;
    }

    syscall1(SYS_PUTS, (u64)(size_t)"DOOM: calling doomgeneric_Create\n");
    doomgeneric_Create(myargc, myargv);

    syscall1(SYS_PUTS, (u64)(size_t)"DOOM: Create done, entering tick loop\n");

    while (1) {
        doomgeneric_Tick();
    }
}
