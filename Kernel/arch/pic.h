#ifndef _KERNEL_ARCH_PIC_H_
#define _KERNEL_ARCH_PIC_H_

#include "types.h"

void KeInitPic(void);
void PicSendEoi(u8 irq);
void PicMaskIrq(u8 irq);
void PicUnmaskIrq(u8 irq);
void PicMaskAll(void);

#endif
