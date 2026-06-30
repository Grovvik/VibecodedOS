#include "e1000.h"
#include "pci.h"
#include "hal.h"
#include "debug.h"
#include "runtime.h"
#include "pmm.h"
#include "vmm.h"
#include "error.h"
#include <intrin.h>

#define E1000_CTRL      0x00000
#define E1000_STATUS    0x00008
#define E1000_EECD      0x00010
#define E1000_EERD      0x00014
#define E1000_IMC       0x000D8
#define E1000_ICR       0x000C0
#define E1000_RCTL      0x00100
#define E1000_TCTL      0x00400
#define E1000_RDBAL     0x02800
#define E1000_RDBAH     0x02804
#define E1000_RDLEN     0x02808
#define E1000_RDH       0x02810
#define E1000_RDT       0x02818
#define E1000_TDBAL     0x03800
#define E1000_TDBAH     0x03804
#define E1000_TDLEN     0x03808
#define E1000_TDH       0x03810
#define E1000_TDT       0x03818
#define E1000_RAL       0x05400
#define E1000_RAH       0x05404
#define E1000_MTA       0x05200

#define E1000_CTRL_RST       0x04000000
#define E1000_CTRL_ASDE      0x00000020
#define E1000_CTRL_SLU       0x00000040

#define E1000_RCTL_EN        0x00000002
#define E1000_RCTL_SBP       0x00000004
#define E1000_RCTL_UPE       0x00000008
#define E1000_RCTL_MPE       0x00000010
#define E1000_RCTL_LPE       0x00000020
#define E1000_RCTL_BAM       0x00008000
#define E1000_RCTL_BSIZE_2048 0x00000000
#define E1000_RCTL_BSEX      0x02000000
#define E1000_RCTL_SECRC     0x04000000

#define E1000_TCTL_EN        0x00000002
#define E1000_TCTL_PSP       0x00000008
#define E1000_TCTL_COLD      0x00040000

#define E1000_TXD_CMD_EOP    0x01
#define E1000_TXD_CMD_IFCS   0x02
#define E1000_TXD_CMD_RS     0x08

#define E1000_TXD_STAT_DD    0x01

#define E1000_RXD_STAT_DD    0x01
#define E1000_RXD_STAT_EOP   0x02

#define E1000_NUM_RX_DESC    32
#define E1000_NUM_TX_DESC    32
#define E1000_BUF_SIZE       2048

#pragma pack(push, 1)
typedef struct {
    u64 addr;
    u16 length;
    u8  cso;
    u8  cmd;
    u8  sta;
    u8  css;
    u16 special;
} E1000TxDesc;

typedef struct {
    u64 addr;
    u16 length;
    u16 checksum;
    u8  sta;
    u8  err;
    u16 special;
} E1000RxDesc;
#pragma pack(pop)

typedef struct {
    u64 mmio_base;
    u8  initialized;
    MacAddr mac;

    /* TX */
    u64 tx_desc_phys;
    E1000TxDesc* tx_desc;
    u64 tx_buf_phys[E1000_NUM_TX_DESC];
    u16 tx_tail;

    /* RX */
    u64 rx_desc_phys;
    E1000RxDesc* rx_desc;
    u64 rx_buf_phys[E1000_NUM_RX_DESC];
    u16 rx_tail;
} E1000Controller;

static E1000Controller g_e1000;

static u32 E1000Read32(u64 base, u32 offset) {
    return *(volatile u32*)(usize)(base + offset);
}

static void E1000Write32(u64 base, u32 offset, u32 value) {
    *(volatile u32*)(usize)(base + offset) = value;
}

static u16 E1000ReadEeprom(u64 base, u8 addr) {
    E1000Write32(base, E1000_EERD, 1 | ((u32)addr << 8));
    u32 timeout = 10000;
    while (timeout--) {
        u32 data = E1000Read32(base, E1000_EERD);
        if (data & 0x10) return (u16)(data >> 16);
    }
    return 0xFFFF;
}

static void E1000ReadMac(u64 base, MacAddr* mac) {
    u32 ral = E1000Read32(base, E1000_RAL);
    u32 rah = E1000Read32(base, E1000_RAH);

    if (ral != 0 || (rah & 0xFFFF) != 0) {
        mac->addr[0] = (u8)(ral & 0xFF);
        mac->addr[1] = (u8)((ral >> 8) & 0xFF);
        mac->addr[2] = (u8)((ral >> 16) & 0xFF);
        mac->addr[3] = (u8)((ral >> 24) & 0xFF);
        mac->addr[4] = (u8)(rah & 0xFF);
        mac->addr[5] = (u8)((rah >> 8) & 0xFF);
        return;
    }

    u16 w0 = E1000ReadEeprom(base, 0);
    u16 w1 = E1000ReadEeprom(base, 1);
    u16 w2 = E1000ReadEeprom(base, 2);

    if (w0 != 0xFFFF) {
        mac->addr[0] = (u8)(w0 & 0xFF);
        mac->addr[1] = (u8)((w0 >> 8) & 0xFF);
        mac->addr[2] = (u8)(w1 & 0xFF);
        mac->addr[3] = (u8)((w1 >> 8) & 0xFF);
        mac->addr[4] = (u8)(w2 & 0xFF);
        mac->addr[5] = (u8)((w2 >> 8) & 0xFF);
        return;
    }

    /* Fallback MAC */
    mac->addr[0] = 0x52;
    mac->addr[1] = 0x54;
    mac->addr[2] = 0x00;
    mac->addr[3] = 0x12;
    mac->addr[4] = 0x34;
    mac->addr[5] = 0x56;
}

static void E1000WriteMac(u64 base, const MacAddr* mac) {
    u32 ral = (u32)mac->addr[0]
            | ((u32)mac->addr[1] << 8)
            | ((u32)mac->addr[2] << 16)
            | ((u32)mac->addr[3] << 24);
    u32 rah = (u32)mac->addr[4]
            | ((u32)mac->addr[5] << 8)
            | (1u << 31); /* AV */
    E1000Write32(base, E1000_RAL, ral);
    E1000Write32(base, E1000_RAH, rah);
}

static i32 E1000DetectEeprom(u64 base) {
    E1000Write32(base, E1000_EECD, 1);
    u32 timeout = 1000;
    while (timeout--) {
        u32 eecd = E1000Read32(base, E1000_EECD);
        if (eecd & 2) return 1; /* EEPROM present */
        if (eecd & 4) return 0; /* Flash */
    }
    return 0;
}

void E1000Init(void) {
    RtMemSet(&g_e1000, 0, sizeof(g_e1000));

    PciDevice pci_dev;
    u32 status = PciFindDevice(&pci_dev, 0x02, 0x00, 0xFF);
    if (NT_ERROR(status)) {
        KdPrintf("[E1000] No network controller found\n");
        return;
    }

    /* Check for Intel vendor ID */
    if (pci_dev.vendor_id != 0x8086) {
        KdPrintf("[E1000] Found non-Intel NIC %04x:%04x\n", pci_dev.vendor_id, pci_dev.device_id);
        /* Continue anyway - many QEMU configs use 0x8086 */
    }

    KdPrintf("[E1000] Found controller: %04x:%04x at %02x:%02x.%d\n",
             pci_dev.vendor_id, pci_dev.device_id,
             pci_dev.bus, pci_dev.device, pci_dev.function);

    u64 mmio = PciGetBarAddress(pci_dev.bus, pci_dev.device, pci_dev.function, 0);
    if (!mmio) {
        KdPrintf("[E1000] BAR0 not found\n");
        return;
    }
    VmmMapPages(VmmGetPml4(), PHYS_TO_VIRT(mmio & PAGE_MASK), mmio & PAGE_MASK, 32, VMM_KERNEL_FLAGS | VMM_NX);
    mmio = (u64)PHYS_TO_VIRT(mmio);
    g_e1000.mmio_base = mmio;

    /* Enable PCI bus master and memory space */
    u32 cmd_reg = PciReadConfig(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04);
    PciWriteConfig(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04, cmd_reg | 0x07);

    /* Reset */
    E1000Write32(mmio, E1000_CTRL, E1000_CTRL_RST);
    u32 timeout = 100000;
    while (timeout-- && (E1000Read32(mmio, E1000_CTRL) & E1000_CTRL_RST));

    /* Disable interrupts */
    E1000Write32(mmio, E1000_IMC, 0xFFFFFFFF);
    (void)E1000Read32(mmio, E1000_ICR);

    /* Read MAC */
    E1000ReadMac(mmio, &g_e1000.mac);
    KdPrintf("[E1000] MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
             g_e1000.mac.addr[0], g_e1000.mac.addr[1], g_e1000.mac.addr[2],
             g_e1000.mac.addr[3], g_e1000.mac.addr[4], g_e1000.mac.addr[5]);

    /* Write MAC back to ensure valid */
    E1000WriteMac(mmio, &g_e1000.mac);

    /* Clear multicast table */
    for (i32 i = 0; i < 128; i++) {
        E1000Write32(mmio, E1000_MTA + i * 4, 0);
    }

    /* Allocate TX descriptors */
    g_e1000.tx_desc_phys = PmmAllocPage();
    if (!g_e1000.tx_desc_phys) {
        KdPrintf("[E1000] OOM TX desc\n");
        return;
    }
    g_e1000.tx_desc = (E1000TxDesc*)PHYS_TO_VIRT(g_e1000.tx_desc_phys);
    RtMemSet(g_e1000.tx_desc, 0, 4096);

    for (i32 i = 0; i < E1000_NUM_TX_DESC; i++) {
        g_e1000.tx_buf_phys[i] = PmmAllocPage();
        if (!g_e1000.tx_buf_phys[i]) {
            KdPrintf("[E1000] OOM TX buffer\n");
            return;
        }
        g_e1000.tx_desc[i].addr = g_e1000.tx_buf_phys[i];
        g_e1000.tx_desc[i].sta = E1000_TXD_STAT_DD;
    }

    /* Allocate RX descriptors */
    g_e1000.rx_desc_phys = PmmAllocPage();
    if (!g_e1000.rx_desc_phys) {
        KdPrintf("[E1000] OOM RX desc\n");
        return;
    }
    g_e1000.rx_desc = (E1000RxDesc*)PHYS_TO_VIRT(g_e1000.rx_desc_phys);
    RtMemSet(g_e1000.rx_desc, 0, 4096);

    for (i32 i = 0; i < E1000_NUM_RX_DESC; i++) {
        g_e1000.rx_buf_phys[i] = PmmAllocPage();
        if (!g_e1000.rx_buf_phys[i]) {
            KdPrintf("[E1000] OOM RX buffer\n");
            return;
        }
        g_e1000.rx_desc[i].addr = g_e1000.rx_buf_phys[i];
        g_e1000.rx_desc[i].sta = 0;
    }

    /* Setup TX ring */
    E1000Write32(mmio, E1000_TDBAL, (u32)(g_e1000.tx_desc_phys & 0xFFFFFFFF));
    E1000Write32(mmio, E1000_TDBAH, (u32)(g_e1000.tx_desc_phys >> 32));
    E1000Write32(mmio, E1000_TDLEN, E1000_NUM_TX_DESC * sizeof(E1000TxDesc));
    E1000Write32(mmio, E1000_TDH, 0);
    E1000Write32(mmio, E1000_TDT, 0);

    /* Setup RX ring */
    E1000Write32(mmio, E1000_RDBAL, (u32)(g_e1000.rx_desc_phys & 0xFFFFFFFF));
    E1000Write32(mmio, E1000_RDBAH, (u32)(g_e1000.rx_desc_phys >> 32));
    E1000Write32(mmio, E1000_RDLEN, E1000_NUM_RX_DESC * sizeof(E1000RxDesc));
    E1000Write32(mmio, E1000_RDH, 0);
    E1000Write32(mmio, E1000_RDT, E1000_NUM_RX_DESC - 1);
    g_e1000.rx_tail = E1000_NUM_RX_DESC - 1;

    /* Enable RX */
    u32 rctl = E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_UPE | E1000_RCTL_MPE
             | E1000_RCTL_LPE | E1000_RCTL_BAM | E1000_RCTL_SECRC;
    E1000Write32(mmio, E1000_RCTL, rctl);

    /* Enable TX */
    E1000Write32(mmio, E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_COLD);

    /* Set link up */
    E1000Write32(mmio, E1000_CTRL, E1000Read32(mmio, E1000_CTRL) | E1000_CTRL_SLU);

    g_e1000.initialized = 1;
    KdPrintf("[E1000] Initialized OK\n");
}

i32 E1000Initialized(void) {
    return g_e1000.initialized;
}

void E1000GetMac(MacAddr* out) {
    if (out) RtMemCopy(out->addr, g_e1000.mac.addr, NET_MAC_SIZE);
}

ntstatus E1000SendFrame(const void* data, u16 len) {
    if (!g_e1000.initialized) return STATUS_UNSUCCESSFUL;
    if (!data || len == 0 || len > E1000_BUF_SIZE) return STATUS_INVALID_PARAMETER;

    u16 tail = g_e1000.tx_tail;
    E1000TxDesc* desc = &g_e1000.tx_desc[tail];

    if (!(desc->sta & E1000_TXD_STAT_DD)) {
        KdPrintf("[E1000] TX ring full at %u\n", tail);
        return STATUS_UNSUCCESSFUL;
    }

    RtMemCopy((void*)PHYS_TO_VIRT(g_e1000.tx_buf_phys[tail]), data, len);
    desc->length = len;
    desc->cso = 0;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->css = 0;
    desc->special = 0;
    desc->sta = 0;

    _ReadWriteBarrier();
    _mm_mfence();

    tail = (tail + 1) % E1000_NUM_TX_DESC;
    g_e1000.tx_tail = tail;
    E1000Write32(g_e1000.mmio_base, E1000_TDT, tail);

    return STATUS_SUCCESS;
}

i32 E1000ReceiveFrame(void* out_buf, u16 max_len) {
    if (!g_e1000.initialized) return -1;
    if (!out_buf || max_len == 0) return -1;

    u16 next = (g_e1000.rx_tail + 1) % E1000_NUM_RX_DESC;
    E1000RxDesc* desc = &g_e1000.rx_desc[next];

    if (!(desc->sta & E1000_RXD_STAT_DD)) {
        return 0; /* No packet available */
    }

    u16 len = desc->length;
    if (len > max_len) len = max_len;

    RtMemCopy(out_buf, (void*)PHYS_TO_VIRT(g_e1000.rx_buf_phys[next]), len);

    /* Give descriptor back to hardware */
    desc->sta = 0;
    _ReadWriteBarrier();

    g_e1000.rx_tail = next;
    E1000Write32(g_e1000.mmio_base, E1000_RDT, next);

    return (i32)len;
}
