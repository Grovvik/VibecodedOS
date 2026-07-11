#include "keyboard.h"
#include "hal.h"
#include "debug.h"

#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64
#define KBD_COMMAND_PORT 0x64

#define KBD_BUFFER_SIZE 256
#define KBD_EVENT_BUFFER_SIZE 256

static volatile char g_kbd_buffer[KBD_BUFFER_SIZE];
static volatile u32  g_kbd_read_pos = 0;
static volatile u32  g_kbd_write_pos = 0;

static volatile KeyEvent g_kbd_events[KBD_EVENT_BUFFER_SIZE];
static volatile u32  g_kbd_event_read = 0;
static volatile u32  g_kbd_event_write = 0;

static volatile i32  g_shift_state = 0;
static volatile i32  g_ctrl_state = 0;
static volatile i32  g_caps_lock = 0;
static volatile i32  g_extended = 0;

static const char g_scancode_to_ascii[] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' '
};

static const char g_scancode_shift_ascii[] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' '
};

static i32 IsBufferEmpty(void) {
    return g_kbd_read_pos == g_kbd_write_pos;
}

static void BufferPut(char c) {
    u32 next = (g_kbd_write_pos + 1) % KBD_BUFFER_SIZE;
    if (next != g_kbd_read_pos) {
        g_kbd_buffer[g_kbd_write_pos] = c;
        g_kbd_write_pos = next;
    }
}

static char BufferGet(void) {
    if (IsBufferEmpty()) return 0;
    char c = g_kbd_buffer[g_kbd_read_pos];
    g_kbd_read_pos = (g_kbd_read_pos + 1) % KBD_BUFFER_SIZE;
    return c;
}

static void EventBufferPut(KeyEvent* ev) {
    u32 next = (g_kbd_event_write + 1) % KBD_EVENT_BUFFER_SIZE;
    if (next != g_kbd_event_read) {
        g_kbd_events[g_kbd_event_write] = *ev;
        g_kbd_event_write = next;
    }
}

static i32 IsEventBufferEmpty(void) {
    return g_kbd_event_read == g_kbd_event_write;
}

void KeInitKeyboard(void) {
    KdPrintf("[KBD] Initializing PS/2 keyboard driver...\n");

    g_kbd_read_pos = 0;
    g_kbd_write_pos = 0;
    g_shift_state = 0;
    g_ctrl_state = 0;
    g_caps_lock = 0;
    g_extended = 0;

    while (HalInByte(KBD_STATUS_PORT) & 0x01) {
        HalInByte(KBD_DATA_PORT);
    }

    HalOutByte(KBD_COMMAND_PORT, 0x60);
    HalIoWait();
    HalOutByte(KBD_DATA_PORT, 0x61);
    HalIoWait();

    HalOutByte(KBD_COMMAND_PORT, 0xAE);
    HalIoWait();

    KdPrintf("[KBD] Keyboard initialized OK\n");
}

void KeyboardIrqHandler(TrapFrame* frame) {
    u8 scancode = HalInByte(KBD_DATA_PORT);
    KdPrintf("[KBD] IRQ: scancode=0x%02x\n", scancode);
    u8 raw = scancode & 0x7F;
    u8 released = scancode & 0x80;

    if (raw == 0xE0) {
        g_extended = 1;
        return;
    }

    if (g_extended) {
        g_extended = 0;
        KeyEvent ev;
        ev.pressed = released ? 0 : 1;
        if (raw == 0x2A || raw == 0x36) { ev.scancode = 0x100 | raw; EventBufferPut(&ev); return; }
        if (raw == 0x1D) { g_ctrl_state = released ? 0 : 1; ev.scancode = 0x100 | 0x1D; EventBufferPut(&ev); return; }
        if (raw == 0x38) { ev.scancode = 0x100 | 0x38; EventBufferPut(&ev); return; }
        if (raw == 0x48) { ev.scancode = 0x148; BufferPut(72); EventBufferPut(&ev); return; }
        if (raw == 0x50) { ev.scancode = 0x150; BufferPut(80); EventBufferPut(&ev); return; }
        if (raw == 0x4B) { ev.scancode = 0x14B; BufferPut(75); EventBufferPut(&ev); return; }
        if (raw == 0x4D) { ev.scancode = 0x14D; BufferPut(77); EventBufferPut(&ev); return; }
        if (raw == 0x52) { ev.scancode = 0x152; EventBufferPut(&ev); return; }
        if (raw == 0x53) { ev.scancode = 0x153; if (!released) BufferPut('\b'); EventBufferPut(&ev); return; }
        ev.scancode = 0x100 | raw;
        EventBufferPut(&ev);
        return;
    }

    switch (raw) {
    case 0x2A:
    case 0x36:
        g_shift_state = released ? 0 : 1;
        {
            KeyEvent ev;
            ev.scancode = raw;
            ev.pressed = released ? 0 : 1;
            EventBufferPut(&ev);
        }
        return;
    case 0x1D:
        g_ctrl_state = released ? 0 : 1;
        {
            KeyEvent ev;
            ev.scancode = raw;
            ev.pressed = released ? 0 : 1;
            EventBufferPut(&ev);
        }
        return;
    case 0x3A:
        if (!released) g_caps_lock = !g_caps_lock;
        {
            KeyEvent ev;
            ev.scancode = raw;
            ev.pressed = released ? 0 : 1;
            EventBufferPut(&ev);
        }
        return;
    default:
        break;
    }

    {
        KeyEvent ev;
        ev.scancode = raw;
        ev.pressed = released ? 0 : 1;
        EventBufferPut(&ev);
    }

    if (released) return;

    if (raw < sizeof(g_scancode_to_ascii)) {
        char c;
        i32 shifted = g_shift_state ^ g_caps_lock;

        if (shifted && raw < sizeof(g_scancode_shift_ascii)) {
            c = g_scancode_shift_ascii[raw];
        } else {
            c = g_scancode_to_ascii[raw];
        }

        if (g_ctrl_state && c >= 'a' && c <= 'z') c -= 0x60;
        if (g_ctrl_state && c >= 'A' && c <= 'Z') c -= 0x40;

        if (c) {
            BufferPut(c);
        }
    }
}

i32 KeyboardHasChar(void) {
    return !IsBufferEmpty();
}

char KeyboardGetChar(void) {
    while (IsBufferEmpty()) {
        HalHlt();
    }
    return BufferGet();
}

i32 KeyboardGetKey(void) {
    if (IsBufferEmpty()) return -1;
    return (i32)BufferGet();
}

i32 KeyboardHasKeyEvent(void) {
    return !IsEventBufferEmpty();
}

i32 KeyboardGetKeyEvent(KeyEvent* ev) {
    if (IsEventBufferEmpty()) return 0;
    *ev = g_kbd_events[g_kbd_event_read];
    g_kbd_event_read = (g_kbd_event_read + 1) % KBD_EVENT_BUFFER_SIZE;
    return 1;
}
