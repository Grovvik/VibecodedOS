#ifndef _KERNEL_KEYBOARD_H_
#define _KERNEL_KEYBOARD_H_

#include "types.h"
#include "cpu.h"

typedef struct {
    u32 scancode;
    u32 pressed;
} KeyEvent;

void KeInitKeyboard(void);
void KeyboardIrqHandler(TrapFrame* frame);
char KeyboardGetChar(void);
i32  KeyboardHasChar(void);
i32  KeyboardGetKey(void);
i32  KeyboardGetKeyEvent(KeyEvent* ev);
i32  KeyboardHasKeyEvent(void);

#endif
