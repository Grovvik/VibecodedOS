#include "pci.h"
#include "hal.h"
#include "debug.h"
#include "error.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_ENABLE_BIT     0x80000000

u32 PciReadConfig(u8 bus, u8 device, u8 func, u8 offset) {
    u32 addr = PCI_ENABLE_BIT
             | ((u32)bus << 16)
             | ((u32)device << 11)
             | ((u32)func << 8)
             | ((u32)offset & 0xFC);
    HalOutDword(PCI_CONFIG_ADDRESS, addr);
    return HalInDword(PCI_CONFIG_DATA);
}

void PciWriteConfig(u8 bus, u8 device, u8 func, u8 offset, u32 value) {
    u32 addr = PCI_ENABLE_BIT
             | ((u32)bus << 16)
             | ((u32)device << 11)
             | ((u32)func << 8)
             | ((u32)offset & 0xFC);
    HalOutDword(PCI_CONFIG_ADDRESS, addr);
    HalOutDword(PCI_CONFIG_DATA, value);
}

void PciInit(void) {
    KdPrintf("[PCI] Scanning PCI bus...\n");
    i32 found = 0;
    for (u32 bus = 0; bus < 256; bus++) {
        for (u32 dev = 0; dev < 32; dev++) {
            u32 vend = PciReadConfig((u8)bus, (u8)dev, 0, 0);
            if (vend == 0xFFFFFFFF || vend == 0x00000000) continue;

            u32 class_word = PciReadConfig((u8)bus, (u8)dev, 0, 0x08);
            u8 cc = (u8)(class_word >> 24);
            u8 sc = (u8)(class_word >> 16);
            u8 pi = (u8)(class_word >> 8);

            KdPrintf("[PCI] %02x:%02x.%d vendor=%04x device=%04x class=%02x/%02x/%02x\n",
                     bus, dev, 0, vend & 0xFFFF, vend >> 16, cc, sc, pi);
            found++;
        }
    }
    KdPrintf("[PCI] Found %d PCI devices\n", found);
}

u64 PciGetBarAddress(u8 bus, u8 device, u8 func, u8 bar_index) {
    u8 offset = 0x10 + bar_index * 4;
    u32 bar_lo = PciReadConfig(bus, device, func, offset);

    if (bar_lo & 1) {
        return (u64)(bar_lo & ~0x3);
    }

    if ((bar_lo & 0x6) == 0x4) {
        u32 bar_hi = PciReadConfig(bus, device, func, offset + 4);
        return ((u64)bar_hi << 32) | (bar_lo & ~0xF);
    }

    return (u64)(bar_lo & ~0xF);
}

u32 PciFindDevice(PciDevice* out, u8 class_code, u8 subclass, u8 prog_if) {
    for (u32 bus = 0; bus < 256; bus++) {
        for (u32 dev = 0; dev < 32; dev++) {
            for (u32 func = 0; func < 8; func++) {
                u32 vend = PciReadConfig((u8)bus, (u8)dev, (u8)func, 0);
                if (vend == 0xFFFFFFFF || vend == 0x00000000) continue;

                u32 class_word = PciReadConfig((u8)bus, (u8)dev, (u8)func, 0x08);
                u8 cc = (u8)(class_word >> 24);
                u8 sc = (u8)(class_word >> 16);
                u8 pi = (u8)(class_word >> 8);

                if (cc == class_code && (subclass == 0xFF || sc == subclass) && (prog_if == 0xFF || pi == prog_if)) {
                    out->bus = (u8)bus;
                    out->device = (u8)dev;
                    out->function = (u8)func;
                    out->vendor_id = vend & 0xFFFF;
                    out->device_id = vend >> 16;
                    out->class_code = cc;
                    out->subclass = sc;
                    out->prog_if = pi;

                    u32 ht = PciReadConfig((u8)bus, (u8)dev, (u8)func, 0x0C);
                    out->header_type = (u8)(ht >> 16) & 0xFF;

                    for (i32 i = 0; i < 6; i++) {
                        out->bar[i] = (u32)PciGetBarAddress((u8)bus, (u8)dev, (u8)func, (u8)i);
                    }

                    u32 irq_word = PciReadConfig((u8)bus, (u8)dev, (u8)func, 0x3C);
                    out->irq = (u8)(irq_word & 0xFF);

                    return STATUS_SUCCESS;
                }
            }
        }
    }
    return STATUS_NOT_FOUND;
}
