#ifndef _KERNEL_SERIAL_H_
#define _KERNEL_SERIAL_H_

#include "types.h"

#define SERIAL_COM1 0x3F8
#define SERIAL_COM2 0x2F8

void SerialInit(void);
void SerialPutChar(char c);
i32  SerialReadChar(void);

#endif
