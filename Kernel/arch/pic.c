#include "pic.h"
#include "hal.h"
#include "debug.h"

#define PIC1_COMMAND  0x20
#define PIC1_DATA     0x21
#define PIC2_COMMAND  0xA0
#define PIC2_DATA     0xA1

#define PIC_EOI       0x20

#define ICW1_ICW4     0x01
#define ICW1_SINGLE   0x02
#define ICW1_INTERVAL4 0x04
#define ICW1_LEVEL    0x08
#define ICW1_INIT     0x10

#define ICW4_8086     0x01
#define ICW4_AUTO     0x02
#define ICW4_BUF_SLAVE 0x08
#define ICW4_BUF_MASTER 0x0C
#define ICW4_SFNM     0x10

#define IRQ_BASE      32

static u16 g_pic_mask = 0xFFFB;

void KeInitPic(void) {
    KdPrintf("[PIC] Initializing 8259A PIC (remap IRQ0-15 -> INT 32-47)...\n");

    HalOutByte(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    HalIoWait();
    HalOutByte(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    HalIoWait();

    HalOutByte(PIC1_DATA, IRQ_BASE);
    HalIoWait();
    HalOutByte(PIC2_DATA, IRQ_BASE + 8);
    HalIoWait();

    HalOutByte(PIC1_DATA, 4);
    HalIoWait();
    HalOutByte(PIC2_DATA, 2);
    HalIoWait();

    HalOutByte(PIC1_DATA, ICW4_8086);
    HalIoWait();
    HalOutByte(PIC2_DATA, ICW4_8086);
    HalIoWait();

    HalOutByte(PIC1_DATA, (u8)(g_pic_mask & 0xFF));
    HalIoWait();
    HalOutByte(PIC2_DATA, (u8)((g_pic_mask >> 8) & 0xFF));
    HalIoWait();

    KdPrintf("[PIC] PIC initialized OK (IRQ2 cascade unmasked)\n");
}

void PicSendEoi(u8 irq) {
    if (irq >= 8) {
        HalOutByte(PIC2_COMMAND, PIC_EOI);
    }
    HalOutByte(PIC1_COMMAND, PIC_EOI);
}

void PicMaskIrq(u8 irq) {
    g_pic_mask |= (1 << irq);
    if (irq < 8) {
        HalOutByte(PIC1_DATA, (u8)(g_pic_mask & 0xFF));
    } else {
        HalOutByte(PIC2_DATA, (u8)((g_pic_mask >> 8) & 0xFF));
    }
}

void PicUnmaskIrq(u8 irq) {
    g_pic_mask &= ~(1 << irq);
    if (irq < 8) {
        HalOutByte(PIC1_DATA, (u8)(g_pic_mask & 0xFF));
    } else {
        HalOutByte(PIC2_DATA, (u8)((g_pic_mask >> 8) & 0xFF));
    }
    KdPrintf("[PIC] Unmasked IRQ %u, mask=0x%04x\n", irq, g_pic_mask);
}

void PicMaskAll(void) {
    g_pic_mask = 0xFFFF;
    HalOutByte(PIC1_DATA, 0xFF);
    HalOutByte(PIC2_DATA, 0xFF);
    KdPrintf("[PIC] All IRQs masked\n");
}
