#include "ahci.h"
#include "pci.h"
#include "hal.h"
#include "debug.h"
#include "runtime.h"
#include "pmm.h"
#include "vmm.h"
#include "error.h"
#include "pit.h"

#define AHCI_CAP       0x00
#define AHCI_GHC       0x04
#define AHCI_PI        0x0C
#define AHCI_VS        0x10

#define AHCI_GHC_AE   0x80000000
#define AHCI_GHC_IE   0x00000002

#define AHCI_PORT_CLB       0x00
#define AHCI_PORT_CLBU      0x04
#define AHCI_PORT_FB        0x08
#define AHCI_PORT_FBU       0x0C
#define AHCI_PORT_IS        0x10
#define AHCI_PORT_IE        0x14
#define AHCI_PORT_CMD       0x18
#define AHCI_PORT_TFD       0x20
#define AHCI_PORT_SIG       0x24
#define AHCI_PORT_SSTS      0x28
#define AHCI_PORT_SCTL      0x2C
#define AHCI_PORT_SERR      0x30
#define AHCI_PORT_SACT      0x34
#define AHCI_PORT_CI        0x38
#define AHCI_PORT_SNTF      0x3C

#define AHCI_PORT_CMD_ST    0x0001
#define AHCI_PORT_CMD_SUD   0x0002
#define AHCI_PORT_CMD_POD   0x0004
#define AHCI_PORT_CMD_FRE   0x0010
#define AHCI_PORT_CMD_FR    0x8000
#define AHCI_PORT_CMD_CR    0x8000
#define AHCI_PORT_CMD_CLO   0x0008

#define AHCI_SSTS_DET_MASK  0x0F
#define AHCI_SSTS_DET_ACTIVE 0x03

#define AHCI_SIG_SATA     0x00000101
#define AHCI_SIG_ATAPI    0xEB140101
#define AHCI_SIG_SEMB     0xC33C0101

#define AHCI_CMD_IDENTIFY 0xEC
#define AHCI_CMD_READ_DMA 0x60
#define AHCI_CMD_WRITE_DMA 0x61

#define AHCI_PRD_COUNT_MAX 8

typedef struct {
    u32 dba;
    u32 dbau;
    u32 reserved;
    u32 dbc;
} AhciPrd;

typedef struct {
    u8  cfis[64];
    u8  atapi[96];
    u8  reserved[48];
    AhciPrd prd[1];
} AhciCmdTable;

typedef struct {
    u16 prdtl;
    u16 prdbc;
    u32 cfl;
    u8  command;
    u8  a;
    u8  c,
         b,
         p,
         r,
         u,
         w;
    u8  pmp;
    u8  reserved;
    u32 ctba;
    u32 ctbau;
    u32 reserved2[4];
} AhciCmdHeader;

#define AHCI_CMD_HEADER_SIZE 32

typedef struct {
    u64 mmio_base;
    u32 port_count;
    u32 active_port;
    u64 cmd_list_phys;
    u64 cmd_table_phys;
    u64 fis_phys;
    AhciCmdHeader* cmd_list;
    AhciCmdTable*  cmd_table;
    u8*            fis_buf;
    u8  lba48;
    u32 sector_count;
} AhciPort;

static AhciPort g_ahci_port;
static i32 g_ahci_initialized;

static u32 AhciRead32(u64 base, u32 offset) {
    return *(volatile u32*)(usize)(base + offset);
}

static void AhciWrite32(u64 base, u32 offset, u32 value) {
    *(volatile u32*)(usize)(base + offset) = value;
}

static void AhciWait(u32 ms) {
    u64 ticks = KeGetTickCount();
    while (KeGetTickCount() - ticks < (u64)ms / 10) {
        HalHlt();
    }
}

static ntstatus AhciPortStart(u64 port_base) {
    u32 cmd = AhciRead32(port_base, AHCI_PORT_CMD);
    if (cmd & AHCI_PORT_CMD_ST) return STATUS_SUCCESS;

    if (!(cmd & AHCI_PORT_CMD_FRE)) {
        AhciWrite32(port_base, AHCI_PORT_CMD, cmd | AHCI_PORT_CMD_FRE);
        u32 timeout = 100000;
        while (timeout-- && !(AhciRead32(port_base, AHCI_PORT_CMD) & AHCI_PORT_CMD_FR));
    }

    AhciWrite32(port_base, AHCI_PORT_CMD, AhciRead32(port_base, AHCI_PORT_CMD) | AHCI_PORT_CMD_ST);
    return STATUS_SUCCESS;
}

static ntstatus AhciPortStop(u64 port_base) {
    u32 cmd = AhciRead32(port_base, AHCI_PORT_CMD);
    if (!(cmd & AHCI_PORT_CMD_ST)) return STATUS_SUCCESS;

    AhciWrite32(port_base, AHCI_PORT_CMD, cmd & ~AHCI_PORT_CMD_ST);

    u32 timeout = 500000;
    while (timeout-- && (AhciRead32(port_base, AHCI_PORT_CMD) & AHCI_PORT_CMD_CR));

    AhciWrite32(port_base, AHCI_PORT_CMD, AhciRead32(port_base, AHCI_PORT_CMD) & ~AHCI_PORT_CMD_FRE);

    timeout = 500000;
    while (timeout-- && (AhciRead32(port_base, AHCI_PORT_CMD) & AHCI_PORT_CMD_FR));

    return STATUS_SUCCESS;
}

static ntstatus AhciIssueCommand(u64 port_base, u8 command, u64 lba, u32 count, void* buffer_phys, i32 is_write) {
    AhciCmdHeader* hdr = g_ahci_port.cmd_list;
    RtMemSet(hdr, 0, sizeof(AhciCmdHeader));

    u32 prdt_count = (count * 512 + 4095) / 4096;
    if (prdt_count > AHCI_PRD_COUNT_MAX) prdt_count = AHCI_PRD_COUNT_MAX;

    hdr->prdtl = (u16)prdt_count;
    hdr->ctba = (u32)(g_ahci_port.cmd_table_phys & 0xFFFFFFFF);
    hdr->ctbau = (u32)(g_ahci_port.cmd_table_phys >> 32);
    hdr->cfl = 5;

    if (is_write) hdr->w = 1;

    AhciCmdTable* tbl = g_ahci_port.cmd_table;
    RtMemSet(tbl, 0, sizeof(AhciCmdTable) - sizeof(AhciPrd) + prdt_count * sizeof(AhciPrd));

    u8* cfis = tbl->cfis;
    cfis[0] = 0x27;
    cfis[1] = (u8)((is_write ? 1 : 0) << 6);
    cfis[2] = command;

    if (g_ahci_port.lba48 && command != AHCI_CMD_IDENTIFY) {
        cfis[3] = 0;
        cfis[4] = (u8)(lba & 0xFF);
        cfis[5] = (u8)((lba >> 8) & 0xFF);
        cfis[6] = (u8)((lba >> 16) & 0xFF);
        cfis[7] = 0;
        cfis[8] = (u8)((lba >> 24) & 0xFF);
        cfis[9] = (u8)((lba >> 32) & 0xFF);
        cfis[10] = (u8)((lba >> 40) & 0xFF);
        cfis[12] = (u8)(count & 0xFF);
        cfis[13] = (u8)((count >> 8) & 0xFF);
    } else if (command == AHCI_CMD_IDENTIFY) {
        /* no LBA for identify */
    } else {
        cfis[4] = (u8)(lba & 0xFF);
        cfis[5] = (u8)((lba >> 8) & 0xFF);
        cfis[6] = (u8)((lba >> 16) & 0xFF);
        cfis[7] = (u8)((lba >> 24) & 0x0F);
        cfis[12] = (u8)(count & 0xFF);
    }

    u64 buf_phys = (u64)(usize)buffer_phys;
    u32 remaining = count * 512;
    u32 offset = 0;
    for (u32 i = 0; i < prdt_count; i++) {
        tbl->prd[i].dba = (u32)((buf_phys + offset) & 0xFFFFFFFF);
        tbl->prd[i].dbau = (u32)((buf_phys + offset) >> 32);
        u32 chunk = remaining > 4096 ? 4096 : remaining;
        tbl->prd[i].dbc = chunk - 1;
        offset += chunk;
        remaining -= chunk;
    }

    AhciWrite32(port_base, AHCI_PORT_IS, AhciRead32(port_base, AHCI_PORT_IS));

    AhciPortStart(port_base);

    AhciWrite32(port_base, AHCI_PORT_CI, 0x01);

    u32 timeout = 5000000;
    while (timeout--) {
        u32 ci = AhciRead32(port_base, AHCI_PORT_CI);
        if (ci == 0) break;

        u32 is = AhciRead32(port_base, AHCI_PORT_IS);
        if (is & 0x40000000) {
            KdPrintf("[AHCI] Task file error: IS=0x%08x TFD=0x%08x\n",
                     is, AhciRead32(port_base, AHCI_PORT_TFD));
            AhciWrite32(port_base, AHCI_PORT_IS, is);
            return STATUS_UNSUCCESSFUL;
        }
    }

    u32 is = AhciRead32(port_base, AHCI_PORT_IS);
    AhciWrite32(port_base, AHCI_PORT_IS, is);

    if (timeout == 0) {
        KdPrintf("[AHCI] Command timeout\n");
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

static ntstatus AhciIdentifyDevice(u64 port_base, u8* identify_buf) {
    u64 buf_phys = PmmAllocPage();
    if (!buf_phys) return STATUS_OUT_OF_MEMORY;

    u64 buf_virt = (u64)PHYS_TO_VIRT(buf_phys);

    ntstatus status = AhciIssueCommand(port_base, AHCI_CMD_IDENTIFY, 0, 0, (void*)(usize)buf_phys, 0);
    if (NT_ERROR(status)) {
        PmmFreePage(buf_phys);
        return status;
    }

    RtMemCopy(identify_buf, (void*)(usize)buf_virt, 512);
    PmmFreePage(buf_phys);
    return STATUS_SUCCESS;
}

void AhciInit(void) {
    g_ahci_initialized = 0;
    RtMemSet(&g_ahci_port, 0, sizeof(g_ahci_port));

    PciDevice pci_dev;
    u32 status = PciFindDevice(&pci_dev, 0x01, 0x06, 0xFF);
    if (NT_ERROR(status)) {
        KdPrintf("[AHCI] No AHCI controller found on PCI\n");
        return;
    }

    KdPrintf("[AHCI] Found AHCI controller: %04x:%04x at %02x:%02x.%d\n",
             pci_dev.vendor_id, pci_dev.device_id,
             pci_dev.bus, pci_dev.device, pci_dev.function);

    u64 mmio = PciGetBarAddress(pci_dev.bus, pci_dev.device, pci_dev.function, 5);
    if (!mmio) {
        KdPrintf("[AHCI] ABAR not found\n");
        return;
    }
    VmmMapPages(VmmGetPml4(), PHYS_TO_VIRT(mmio & PAGE_MASK), mmio & PAGE_MASK, 32, VMM_KERNEL_FLAGS | VMM_NX);
    mmio = (u64)PHYS_TO_VIRT(mmio);
    g_ahci_port.mmio_base = mmio;

    u32 cmd_reg = PciReadConfig(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04);
    PciWriteConfig(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04, cmd_reg | 0x07);

    u32 ghc = AhciRead32(mmio, AHCI_GHC);
    if (!(ghc & AHCI_GHC_AE)) {
        AhciWrite32(mmio, AHCI_GHC, ghc | AHCI_GHC_AE);
    }

    u32 pi = AhciRead32(mmio, AHCI_PI);
    KdPrintf("[AHCI] Ports implemented: 0x%08x\n", pi);

    u64 port_base = 0;
    for (i32 i = 0; i < 32; i++) {
        if (!(pi & (1 << i))) continue;

        u64 pb = mmio + 0x100 + i * 0x80;
        u32 ssts = AhciRead32(pb, AHCI_PORT_SSTS);
        u32 det = ssts & AHCI_SSTS_DET_MASK;

        if (det != AHCI_SSTS_DET_ACTIVE) continue;

        u32 sig = AhciRead32(pb, AHCI_PORT_SIG);
        if (sig != AHCI_SIG_SATA) continue;

        KdPrintf("[AHCI] Active SATA device at port %d (SSTS=0x%08x SIG=0x%08x)\n",
                 i, ssts, sig);
        port_base = pb;
        g_ahci_port.active_port = (u32)i;
        break;
    }

    if (!port_base) {
        KdPrintf("[AHCI] No active SATA device found\n");
        return;
    }

    AhciPortStop(port_base);

    u64 cmd_list_phys = PmmAllocPage();
    u64 cmd_table_phys = PmmAllocPage();
    u64 fis_phys = PmmAllocPage();

    if (!cmd_list_phys || !cmd_table_phys || !fis_phys) {
        KdPrintf("[AHCI] Failed to allocate command structures\n");
        return;
    }

    g_ahci_port.cmd_list_phys = cmd_list_phys;
    g_ahci_port.cmd_table_phys = cmd_table_phys;
    g_ahci_port.fis_phys = fis_phys;
    g_ahci_port.cmd_list = (AhciCmdHeader*)PHYS_TO_VIRT(cmd_list_phys);
    g_ahci_port.cmd_table = (AhciCmdTable*)PHYS_TO_VIRT(cmd_table_phys);
    g_ahci_port.fis_buf = (u8*)PHYS_TO_VIRT(fis_phys);

    RtMemSet(g_ahci_port.cmd_list, 0, 4096);
    RtMemSet(g_ahci_port.cmd_table, 0, 4096);
    RtMemSet(g_ahci_port.fis_buf, 0, 4096);

    AhciWrite32(port_base, AHCI_PORT_CLB, (u32)(cmd_list_phys & 0xFFFFFFFF));
    AhciWrite32(port_base, AHCI_PORT_CLBU, (u32)(cmd_list_phys >> 32));
    AhciWrite32(port_base, AHCI_PORT_FB, (u32)(fis_phys & 0xFFFFFFFF));
    AhciWrite32(port_base, AHCI_PORT_FBU, (u32)(fis_phys >> 32));

    u32 cmd = AhciRead32(port_base, AHCI_PORT_CMD);
    AhciWrite32(port_base, AHCI_PORT_CMD, cmd | AHCI_PORT_CMD_FRE | AHCI_PORT_CMD_POD | AHCI_PORT_CMD_SUD);

    AhciWait(50);

    AhciPortStart(port_base);

    AhciWrite32(port_base, AHCI_PORT_IS, 0xFFFFFFFF);

    u8 identify[512];
    status = AhciIdentifyDevice(port_base, identify);
    if (NT_ERROR(status)) {
        KdPrintf("[AHCI] IDENTIFY DEVICE failed\n");
        return;
    }

    u16 lba48_sectors_lo = *(u16*)(identify + 200);
    u16 lba48_sectors_hi = *(u16*)(identify + 202);
    u16 lba48_sectors_xhi = *(u16*)(identify + 204);
    u16 lba48_sectors_xhi2 = *(u16*)(identify + 206);
    u64 total_sectors_lba48 = (u64)lba48_sectors_xhi2 << 48 |
                               (u64)lba48_sectors_xhi << 32 |
                               (u64)lba48_sectors_hi << 16 |
                               lba48_sectors_lo;

    u32 lba28_sectors = *(u32*)(identify + 120);

    if (identify[167] & 0x01 || identify[173] & 0x01) {
        g_ahci_port.lba48 = 1;
        g_ahci_port.sector_count = (u32)(total_sectors_lba48 & 0xFFFFFFFF);
        KdPrintf("[AHCI] LBA48 supported, sectors=%llu (%llu MB)\n",
                 total_sectors_lba48, total_sectors_lba48 / 2048);
    } else {
        g_ahci_port.lba48 = 0;
        g_ahci_port.sector_count = lba28_sectors;
        KdPrintf("[AHCI] LBA28 only, sectors=%u (%u MB)\n",
                 lba28_sectors, lba28_sectors / 2048);
    }

    g_ahci_initialized = 1;
    KdPrintf("[AHCI] Initialized OK on port %d\n", g_ahci_port.active_port);
}

i32 AhciInitialized(void) {
    return g_ahci_initialized;
}

ntstatus AhciReadSectors(u64 lba, u64 count, void* buffer) {
    if (!g_ahci_initialized) return STATUS_UNSUCCESSFUL;
    if (count == 0 || count > 256) return STATUS_INVALID_PARAMETER;

    u64 port_base = g_ahci_port.mmio_base + 0x100 + g_ahci_port.active_port * 0x80;

    u64 buf_phys = PmmAllocPage();
    if (!buf_phys) return STATUS_OUT_OF_MEMORY;

    ntstatus status = STATUS_SUCCESS;
    u8* dst = (u8*)buffer;

    for (u64 sec = 0; sec < count; sec += 1) {
        u32 chunk = 1;
        status = AhciIssueCommand(port_base, g_ahci_port.lba48 ? 0x24 : AHCI_CMD_READ_DMA,
                                  lba + sec, chunk, (void*)(usize)buf_phys, 0);
        if (NT_ERROR(status)) break;

        RtMemCopy(dst + sec * 512, (void*)PHYS_TO_VIRT(buf_phys), chunk * 512);
    }

    PmmFreePage(buf_phys);
    return status;
}

ntstatus AhciWriteSectors(u64 lba, u64 count, const void* buffer) {
    if (!g_ahci_initialized) return STATUS_UNSUCCESSFUL;
    if (count == 0 || count > 256) return STATUS_INVALID_PARAMETER;

    u64 port_base = g_ahci_port.mmio_base + 0x100 + g_ahci_port.active_port * 0x80;

    u64 buf_phys = PmmAllocPage();
    if (!buf_phys) return STATUS_OUT_OF_MEMORY;

    ntstatus status = STATUS_SUCCESS;
    const u8* src = (const u8*)buffer;

    for (u64 sec = 0; sec < count; sec += 1) {
        RtMemCopy((void*)PHYS_TO_VIRT(buf_phys), src + sec * 512, 512);

        status = AhciIssueCommand(port_base, g_ahci_port.lba48 ? 0x34 : AHCI_CMD_WRITE_DMA,
                                  lba + sec, 1, (void*)(usize)buf_phys, 1);
        if (NT_ERROR(status)) break;
    }

    PmmFreePage(buf_phys);
    return status;
}
