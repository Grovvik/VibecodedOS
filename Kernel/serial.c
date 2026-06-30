#include "serial.h"
#include "hal.h"
#include "debug.h"

#define COM_DATA      0
#define COM_IER       1
#define COM_IIR       2
#define COM_FCR       2
#define COM_LCR       3
#define COM_MCR       4
#define COM_LSR       5
#define COM_MSR       6
#define COM_SCR       7

#define LSR_DR        0x01
#define LSR_OE        0x02
#define LSR_PE        0x04
#define LSR_FE        0x08
#define LSR_BI        0x10
#define LSR_THRE      0x20
#define LSR_TEMT      0x40
#define LSR_FIFO_ERR  0x80

static u16 g_serial_port = SERIAL_COM1;

void SerialInit(void) {
    u16 port = g_serial_port;

    HalOutByte(port + COM_IER, 0x00);
    HalIoWait();

    HalOutByte(port + COM_LCR, 0x80);
    HalIoWait();
    HalOutByte(port + COM_DATA, 0x01);
    HalIoWait();
    HalOutByte(port + COM_IER, 0x00);
    HalIoWait();

    HalOutByte(port + COM_LCR, 0x03);
    HalIoWait();

    HalOutByte(port + COM_FCR, 0xC7);
    HalIoWait();

    HalOutByte(port + COM_MCR, 0x0B);
    HalIoWait();

    KdPrintf("[SERIAL] COM1 initialized at 0x%04x (115200 baud)\n", port);
}

void SerialPutChar(char c) {
    u16 port = g_serial_port;
    while (!(HalInByte(port + COM_LSR) & LSR_THRE));
    HalOutByte(port + COM_DATA, (u8)c);
}

i32 SerialReadChar(void) {
    u16 port = g_serial_port;
    if (HalInByte(port + COM_LSR) & LSR_DR) {
        return (i32)HalInByte(port + COM_DATA);
    }
    return -1;
}
