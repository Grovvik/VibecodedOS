#ifndef _KERNEL_FS_PCI_H_
#define _KERNEL_FS_PCI_H_

#include "types.h"

typedef struct {
    u8  bus;
    u8  device;
    u8  function;
    u16 vendor_id;
    u16 device_id;
    u8  class_code;
    u8  subclass;
    u8  prog_if;
    u8  header_type;
    u32 bar[6];
    u8  irq;
} PciDevice;

void     PciInit(void);
u32      PciReadConfig(u8 bus, u8 device, u8 func, u8 offset);
void     PciWriteConfig(u8 bus, u8 device, u8 func, u8 offset, u32 value);
u32      PciFindDevice(PciDevice* out, u8 class_code, u8 subclass, u8 prog_if);
u64      PciGetBarAddress(u8 bus, u8 device, u8 func, u8 bar_index);

#endif
