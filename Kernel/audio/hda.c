#include "hda.h"
#include "pci.h"
#include "hal.h"
#include "debug.h"
#include "runtime.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "error.h"
#include "pit.h"
#include <intrin.h>

/*
 * Intel High Definition Audio (HDA) Controller Driver
 * Reference: Intel HDA spec + AudioDxe (OpenCore)
 */

#define HDA_GCAP       0x00
#define HDA_GCTL       0x08
#define HDA_WAKEEN     0x0C
#define HDA_STATESTS   0x0E
#define HDA_GSTS       0x10
#define HDA_INTCTL     0x20
#define HDA_INTSTS     0x24

#define HDA_CORBLBASE  0x40
#define HDA_CORBUBASE  0x44
#define HDA_CORBWP     0x48
#define HDA_CORBRP     0x4A
#define HDA_CORBCTL    0x4C
#define HDA_CORBSIZE   0x4E

#define HDA_RIRBLBASE  0x50
#define HDA_RIRBUBASE  0x54
#define HDA_RIRBWP     0x58
#define HDA_RINTCNT    0x5A
#define HDA_RIRBCTL    0x5C
#define HDA_RIRBSIZE   0x5E

#define HDA_IC         0x60
#define HDA_IR         0x64
#define HDA_IRS        0x68

#define HDA_DPLBASE    0x70
#define HDA_DPUBASE    0x74

#define HDA_GCTL_CRST  0x00000001

#define HDA_CORBCTL_RUN 0x02
#define HDA_RIRBCTL_RUN 0x02
#define HDA_RIRBCTL_RINT 0x01

/* Output stream 0 descriptor offsets from mmio + 0x80 */
#define HDA_SD0_BASE     0x80
#define HDA_SDn_CTL      0x00
#define HDA_SDn_STS      0x03
#define HDA_SDn_LPIB     0x04
#define HDA_SDn_CBL      0x08
#define HDA_SDn_LVI      0x0C
#define HDA_SDn_FIFOD    0x10
#define HDA_SDn_FMT      0x12
#define HDA_SDn_BDPL     0x18
#define HDA_SDn_BDPU     0x1C

#define HDA_SDn_CTL_DEIE   0x00100000
#define HDA_SDn_CTL_FEIE   0x00080000
#define HDA_SDn_CTL_IOCE   0x00040000
#define HDA_SDn_CTL_RUN    0x00000002
#define HDA_SDn_CTL_SRST   0x00000001
#define HDA_SDn_CTL_STRIPE 0x000000C0
#define HDA_SDn_CTL_STREAM_SHIFT 20

#define HDA_SDn_STS_FIFOE  0x10
#define HDA_SDn_STS_BCIS   0x04

/* BDL entry */
#pragma pack(push, 1)
typedef struct {
    u64 addr;
    u32 length;
    u32 ioc;
} HdaBdlEntry;
#pragma pack(pop)

#define HDA_MAX_CORB 256
#define HDA_MAX_RIRB 256

typedef struct {
    u64 mmio_base;
    u8  initialized;

    u64 corb_phys;
    volatile u32* corb;
    u16 corb_wp;

    u64 rirb_phys;
    volatile u64* rirb;
    u16 rirb_wp_hw;

    u64 bdl_phys;
    volatile HdaBdlEntry* bdl;

    u64 pcm_phys;
    u32 pcm_size;

    u8  codec;
    u16 dac_node;
    u16 pin_node;

    u8  pci_bus;
    u8  pci_dev;
    u8  pci_func;

    u8  playing;
    u32 bytes_to_play;
    u64 start_tick;
    u64 duration_ticks;
} HdaController;

static HdaController g_hda;

static u32 HdaRead32(u64 base, u32 offset) {
    return *(volatile u32*)(usize)(base + offset);
}
static void HdaWrite32(u64 base, u32 offset, u32 value) {
    *(volatile u32*)(usize)(base + offset) = value;
}
static u16 HdaRead16(u64 base, u32 offset) {
    return *(volatile u16*)(usize)(base + offset);
}
static void HdaWrite16(u64 base, u32 offset, u16 value) {
    *(volatile u16*)(usize)(base + offset) = value;
}
static u8 HdaRead8(u64 base, u32 offset) {
    return *(volatile u8*)(usize)(base + offset);
}
static void HdaWrite8(u64 base, u32 offset, u8 value) {
    *(volatile u8*)(usize)(base + offset) = value;
}

static void HdaWaitMs(u32 ms) {
    volatile u64 count = (u64)ms * 500000ULL;
    while (count--) { __nop(); }
}

/* Build a standard 12-bit verb with 8-bit payload */
static u32 HdaMakeVerb(u8 codec, u16 node, u32 verb, u32 payload) {
    return ((u32)codec << 28)
         | ((node & 0x7F) << 20)
         | ((verb & 0xFFF) << 8)
         | (payload & 0xFF);
}

/* Build a 4-bit verb with 16-bit payload (e.g. 0x2 Set Converter Format, 0x3 Set Amplifier) */
static u32 HdaMakeVerb16(u8 codec, u16 node, u8 verb, u16 payload) {
    return ((u32)codec << 28)
         | ((node & 0x7F) << 20)
         | ((verb & 0xF) << 16)
         | (payload & 0xFFFF);
}

static u64 HdaGetOutputStreamBase(u8 stream_index) {
    /* HDA spec: stream descriptors are contiguous starting at 0x80.
     * Input streams 0..iss-1 come first, then output streams.
     * Output stream 0 base = 0x80 + iss * 0x20 */
    u16 gcap = HdaRead16(g_hda.mmio_base, HDA_GCAP);
    u8 iss = (u8)((gcap >> 8) & 0xF);
    return g_hda.mmio_base + 0x80 + (iss * 0x20) + (stream_index * 0x20);
}

static u32 HdaSendVerb(u32 verb) {
    if (!g_hda.mmio_base) return 0;

    /* Use Immediate Command (IC) interface.
     * Proper state sequence from HDA spec:
     * 1. Wait for ICB=0 and IRV=0 (idle)
     * 2. Write command to IC
     * 3. Write IRS=0x01 (set ICB)
     * 4. Poll until ICB=0 (command sent)
     * 5. Read IR if IRV=1
     * 6. Write IRS=0x02 (clear IRV)
     * 7. Wait for IRV=0
     */

    /* Step 1: Wait for idle state */
    u32 prep = 10000;
    while (prep--) {
        u8 irs = HdaRead8(g_hda.mmio_base, HDA_IRS);
        if ((irs & 0x03) == 0) break;
        if (irs & 0x02) {
            HdaWrite8(g_hda.mmio_base, HDA_IRS, 0x02);
            _mm_mfence();
        }
        __nop();
    }

    HdaWrite32(g_hda.mmio_base, HDA_IC, verb);
    _mm_mfence();

    HdaWrite8(g_hda.mmio_base, HDA_IRS, 0x01);
    _mm_mfence();

    /* Step 4: Poll until ICB clears */
    u32 timeout = 1000000;
    while (timeout--) {
        u8 irs = HdaRead8(g_hda.mmio_base, HDA_IRS);
        if (!(irs & 0x01)) {
            /* Step 5: Read response if IRV is set */
            if (irs & 0x02) {
                u32 resp = HdaRead32(g_hda.mmio_base, HDA_IR);

                /* Step 6: Clear IRV */
                HdaWrite8(g_hda.mmio_base, HDA_IRS, 0x02);
                _mm_mfence();

                /* Step 7: Wait for IRV to clear */
                u32 clr = 10000;
                while (clr--) {
                    if (!(HdaRead8(g_hda.mmio_base, HDA_IRS) & 0x02)) break;
                    __nop();
                }
                return resp;
            }
            /* ICB cleared but IRV not set - no response (shouldn't happen for GetParam) */
            KdPrintf("[HDA] IC no response! verb=0x%08x IRS=0x%02x\n", verb, irs);
            return 0;
        }
        __nop();
    }

    KdPrintf("[HDA] IC verb timeout! verb=0x%08x IRS=0x%02x\n", verb,
             HdaRead8(g_hda.mmio_base, HDA_IRS));
    return 0;
}

static u32 HdaGetParam(u8 codec, u16 node, u8 param) {
    return HdaSendVerb(HdaMakeVerb(codec, node, 0xF00, param));
}

/* Try to disable PCIe No Snoop (cache coherence fix from AudioDxe) */
static void HdaDisableNoSnoop(void) {
    u32 cap_ptr = PciReadConfig(g_hda.pci_bus, g_hda.pci_dev, g_hda.pci_func, 0x34);
    cap_ptr &= 0xFC;
    while (cap_ptr) {
        u32 cap = PciReadConfig(g_hda.pci_bus, g_hda.pci_dev, g_hda.pci_func, (u8)cap_ptr);
        u8 cap_id = (u8)(cap & 0xFF);
        if (cap_id == 0x10) { /* PCI Express capability */
            u32 dev_ctrl = PciReadConfig(g_hda.pci_bus, g_hda.pci_dev, g_hda.pci_func, (u8)(cap_ptr + 8));
            if (dev_ctrl & 0x800) { /* No Snoop Enable bit 11 */
                KdPrintf("[HDA] Disabling PCIe No Snoop\n");
                PciWriteConfig(g_hda.pci_bus, g_hda.pci_dev, g_hda.pci_func,
                               (u8)(cap_ptr + 8), dev_ctrl & ~0x800);
            }
            break;
        }
        cap_ptr = (cap >> 8) & 0xFC;
    }
}

static void HdaResetController(void) {
    KdPrintf("[HDA] Controller reset...\n");

    /* Enable wake events for all codecs so they can signal presence */
    HdaWrite16(g_hda.mmio_base, HDA_WAKEEN, 0xFFFF);

    HdaWrite32(g_hda.mmio_base, HDA_GCTL, 0);
    HdaWaitMs(10);

    /* Poll until CRST clears */
    u32 timeout = 100000;
    while (timeout-- && (HdaRead32(g_hda.mmio_base, HDA_GCTL) & HDA_GCTL_CRST));

    HdaWrite32(g_hda.mmio_base, HDA_GCTL, HDA_GCTL_CRST);
    HdaWaitMs(10);

    /* Poll until CRST sets */
    timeout = 100000;
    while (timeout-- && !(HdaRead32(g_hda.mmio_base, HDA_GCTL) & HDA_GCTL_CRST));

    /* Wait 100ms for codecs to initialize (crucial! from AudioDxe) */
    KdPrintf("[HDA] Reset complete, waiting 100ms for codecs...\n");
    HdaWaitMs(100);
}

static void HdaInitCorb(void) {
    g_hda.corb_phys = PmmAllocPage();
    if (!g_hda.corb_phys) return;
    g_hda.corb = (volatile u32*)PHYS_TO_VIRT(g_hda.corb_phys);
    RtMemSet((void*)(usize)g_hda.corb, 0, 4096);

    HdaWrite8(g_hda.mmio_base, HDA_CORBCTL, 0);
    HdaWaitMs(10);

    HdaWrite32(g_hda.mmio_base, HDA_CORBLBASE, (u32)(g_hda.corb_phys & 0xFFFFFFFF));
    HdaWrite32(g_hda.mmio_base, HDA_CORBUBASE, (u32)(g_hda.corb_phys >> 32));
    HdaWrite16(g_hda.mmio_base, HDA_CORBRP, 0x8000);
    HdaWaitMs(10);
    /* Poll until reset bit clears */
    u32 corb_rst_timeout = 10000;
    while (corb_rst_timeout-- && (HdaRead16(g_hda.mmio_base, HDA_CORBRP) & 0x8000));
    HdaWrite16(g_hda.mmio_base, HDA_CORBRP, 0);
    HdaWrite16(g_hda.mmio_base, HDA_CORBWP, 0);
    HdaWrite16(g_hda.mmio_base, HDA_CORBSIZE, 0x0202);

    g_hda.corb_wp = 0;
    HdaWrite8(g_hda.mmio_base, HDA_CORBCTL, HDA_CORBCTL_RUN);
    HdaWaitMs(10);

    /* Verify RUN bit is set */
    u32 corb_timeout = 10000;
    while (corb_timeout-- && !(HdaRead8(g_hda.mmio_base, HDA_CORBCTL) & HDA_CORBCTL_RUN));

    /* Sync our write pointer with hardware read pointer so we don't
     * overwrite unprocessed entries if the controller already
     * consumed the zero-filled slot 0 during startup. */
    g_hda.corb_wp = HdaRead16(g_hda.mmio_base, HDA_CORBRP) & 0xFF;
}

static void HdaInitRirb(void) {
    g_hda.rirb_phys = PmmAllocPage();
    if (!g_hda.rirb_phys) return;
    g_hda.rirb = (volatile u64*)PHYS_TO_VIRT(g_hda.rirb_phys);
    RtMemSet((void*)(usize)g_hda.rirb, 0, 4096);

    HdaWrite8(g_hda.mmio_base, HDA_RIRBCTL, 0);
    HdaWaitMs(10);

    HdaWrite32(g_hda.mmio_base, HDA_RIRBLBASE, (u32)(g_hda.rirb_phys & 0xFFFFFFFF));
    HdaWrite32(g_hda.mmio_base, HDA_RIRBUBASE, (u32)(g_hda.rirb_phys >> 32));
    HdaWrite16(g_hda.mmio_base, HDA_RIRBWP, 0x8000);
    HdaWaitMs(10);
    /* Poll until reset bit clears */
    u32 rirb_rst_timeout = 10000;
    while (rirb_rst_timeout-- && (HdaRead16(g_hda.mmio_base, HDA_RIRBWP) & 0x8000));
    HdaWrite16(g_hda.mmio_base, HDA_RIRBWP, 0);
    HdaWrite16(g_hda.mmio_base, HDA_RINTCNT, 1);
    HdaWrite16(g_hda.mmio_base, HDA_RIRBSIZE, 0x0202);

    /* Initialize to 0xFF so the very first response at index 0 is detected as a change. */
    g_hda.rirb_wp_hw = 0xFF;
    HdaWrite8(g_hda.mmio_base, HDA_RIRBCTL, HDA_RIRBCTL_RUN | HDA_RIRBCTL_RINT);
    HdaWaitMs(10);

    /* Verify RUN bit is set */
    u32 rirb_timeout = 10000;
    while (rirb_timeout-- && !(HdaRead8(g_hda.mmio_base, HDA_RIRBCTL) & HDA_RIRBCTL_RUN));
}

static i32 HdaFindCodecAndPath(void) {
    /* Poll STATESTS for up to ~1 second; some emulators take a moment */
    u16 statests = 0;
    u32 poll = 100;
    while (poll--) {
        statests = HdaRead16(g_hda.mmio_base, HDA_STATESTS);
        if (statests) break;
        HdaWaitMs(10);
    }
    KdPrintf("[HDA] STATESTS=0x%04x (poll=%u)\n", statests, 100 - poll);

    if (!statests) {
        KdPrintf("[HDA] No codecs detected after polling\n");
        return -1;
    }

    /* Clear sticky STATESTS bits so the controller sees them handled */
    HdaWrite16(g_hda.mmio_base, HDA_STATESTS, statests);

    for (u8 codec = 0; codec < 15; codec++) {
        if (!(statests & (1 << codec))) continue;
        g_hda.codec = codec;
        KdPrintf("[HDA] Codec %d present, probing...\n", codec);

        /* Verify codec responds by reading Vendor ID (param 0x00) */
        u32 vendor_id = HdaGetParam(codec, 0, 0x00);
        KdPrintf("[HDA]   Vendor ID = 0x%08x\n", vendor_id);
        if (vendor_id == 0 || vendor_id == 0xFFFFFFFF) {
            KdPrintf("[HDA]   Codec not responding, skipping\n");
            continue;
        }

        /* Root node 0: find function groups.
         * HDA spec: Root node param 0x04 has Start Node ID in bits 23:16,
         * Total Nodes in bits 7:0 (NOT the same as widget nodes!).
         */
        u32 root_param = HdaGetParam(codec, 0, 0x04);
        u8 fg_count = (u8)(root_param & 0xFF);
        u8 fg_start = (u8)((root_param >> 16) & 0xFF);
        KdPrintf("[HDA]   Root: FG start=%u count=%u (raw=0x%x)\n", fg_start, fg_count, root_param);

        for (u8 f = 0; f < fg_count && f < 8; f++) {
            u16 fg_node = fg_start + f;
            u32 fg_type = HdaGetParam(codec, fg_node, 0x05);
            KdPrintf("[HDA]   FG node %u type=0x%x\n", fg_node, fg_type);

            /* Only Audio Function Group (type 0x01) */
            if ((fg_type & 0x7F) != 0x01) continue;

            /* Audio FG param 0x04: widgets start in bits 15:8, count in bits 7:0 */
            u32 fg_param = HdaGetParam(codec, fg_node, 0x04);
            u8 w_count = (u8)(fg_param & 0xFF);
            u8 w_start = (u8)((fg_param >> 8) & 0xFF);
            KdPrintf("[HDA]     Audio FG: widgets start=%u count=%u (raw=0x%x)\n", w_start, w_count, fg_param);

            g_hda.dac_node = 0xFFFF;
            g_hda.pin_node = 0xFFFF;

            for (u8 i = 0; i < w_count && i < 32; i++) {
                u16 wnode = w_start + i;
                u32 wcap = HdaGetParam(codec, wnode, 0x09);
                if (wcap == 0 || wcap == 0xFFFFFFFF) {
                    KdPrintf("[HDA]       Widget %u wcap=0x%08x (skip, no capabilities)\n", wnode, wcap);
                    continue;
                }
                u32 wtype = (wcap >> 20) & 0xF;
                KdPrintf("[HDA]       Widget %u wcap=0x%08x type=%u\n", wnode, wcap, wtype);

                if (wtype == 0x0 && g_hda.dac_node == 0xFFFF) {
                    g_hda.dac_node = wnode;
                    KdPrintf("[HDA]       -> DAC=%u\n", wnode);
                } else if (wtype == 0x4 && g_hda.pin_node == 0xFFFF) {
                    g_hda.pin_node = wnode;
                    KdPrintf("[HDA]       -> PIN=%u\n", wnode);
                }
            }

            if (g_hda.dac_node != 0xFFFF) {
                if (g_hda.pin_node == 0xFFFF) {
                    g_hda.pin_node = g_hda.dac_node;
                    KdPrintf("[HDA]     No pin found, using DAC as output\n");
                }
                return 0;
            }
        }
    }

    /* Fallback: brute-force scan all possible nodes */
    KdPrintf("[HDA] AFG scan failed, trying brute-force node scan...\n");
    g_hda.dac_node = 0xFFFF;
    g_hda.pin_node = 0xFFFF;
    for (u16 wnode = 2; wnode < 16; wnode++) {
        u32 wcap = HdaGetParam(g_hda.codec, wnode, 0x09);
        if (wcap == 0 || wcap == 0xFFFFFFFF) continue;
        u32 wtype = (wcap >> 20) & 0xF;
        KdPrintf("[HDA]   Fallback scan node %u wcap=0x%08x type=%u\n", wnode, wcap, wtype);
        if (wtype == 0x0 && g_hda.dac_node == 0xFFFF) {
            g_hda.dac_node = wnode;
        } else if (wtype == 0x4 && g_hda.pin_node == 0xFFFF) {
            g_hda.pin_node = wnode;
        }
    }
    if (g_hda.dac_node != 0xFFFF) {
        if (g_hda.pin_node == 0xFFFF) {
            g_hda.pin_node = g_hda.dac_node;
        }
        KdPrintf("[HDA] Fallback found: DAC=%u PIN=%u\n", g_hda.dac_node, g_hda.pin_node);
        return 0;
    }

    KdPrintf("[HDA] No audio path found\n");
    return -1;
}

static void HdaSetupAudioPath(void) {
    u8 c = g_hda.codec;
    u16 dac = g_hda.dac_node;
    u16 pin = g_hda.pin_node;

    /* 1. Set DAC Converter Format: 48kHz, 16-bit, stereo (0x10011) */
    HdaSendVerb(HdaMakeVerb16(c, dac, 0x2, 0x10011));
    HdaWaitMs(1);

    /* 2. Set DAC Stream=1, Channel=0 */
    HdaSendVerb(HdaMakeVerb(c, dac, 0x706, 0x10));
    HdaWaitMs(1);

    /* 3. Unmute DAC output amplifier (both channels, output, index 0, gain 0dB=0x7F) */
    /* 0xE07F = set left, set right, set index=0, mute=0, gain=127 */
    /* Use verb 0x3 (Set Amplifier), NOT 0xB (Get Amplifier) */
    HdaSendVerb(HdaMakeVerb16(c, dac, 0x3, 0xE07F));
    HdaWaitMs(1);

    /* 4. If pin != dac, configure pin complex */
    if (pin != dac) {
        /* Check if pin has connection list */
        u32 conn_len = HdaGetParam(c, pin, 0x0E);
        u8 num_conn = (u8)(conn_len & 0x7F);
        KdPrintf("[HDA] Pin %u connection list length=%u\n", pin, num_conn);

        if (num_conn > 0) {
            /* For simple topologies, just select connection index 0 (should be DAC) */
            HdaSendVerb(HdaMakeVerb(c, pin, 0x701, 0)); /* 0x701 is Set Connection Select */
            HdaWaitMs(1);
        }

        /* Pin Widget Control: output enable (0x40) */
        HdaSendVerb(HdaMakeVerb(c, pin, 0x707, 0x40));
        HdaWaitMs(1);

        /* Enable EAPD (External Amplifier Power Down) if present */
        HdaSendVerb(HdaMakeVerb(c, pin, 0x70C, 0x02)); /* 0x70C is Set EAPD/BTL Enable, bit 1 = EAPD */
        HdaWaitMs(1);

        /* Unmute pin output amplifier */
        HdaSendVerb(HdaMakeVerb16(c, pin, 0x3, 0xE07F));
        HdaWaitMs(1);
    }

    KdPrintf("[HDA] Path configured: DAC=%u PIN=%u format=0x10011\n", dac, pin);
}

void HdaInit(void) {
    RtMemSet(&g_hda, 0, sizeof(g_hda));

    PciDevice pci_dev;
    u32 status = PciFindDevice(&pci_dev, 0x04, 0x01, 0xFF);
    if (NT_ERROR(status)) {
        status = PciFindDevice(&pci_dev, 0x04, 0x03, 0xFF);
    }
    if (NT_ERROR(status)) {
        KdPrintf("[HDA] No HD Audio controller found on PCI\n");
        return;
    }

    g_hda.pci_bus = pci_dev.bus;
    g_hda.pci_dev = pci_dev.device;
    g_hda.pci_func = pci_dev.function;

    KdPrintf("[HDA] Found controller: %04x:%04x at %02x:%02x.%d\n",
             pci_dev.vendor_id, pci_dev.device_id,
             pci_dev.bus, pci_dev.device, pci_dev.function);

    u64 mmio = PciGetBarAddress(pci_dev.bus, pci_dev.device, pci_dev.function, 0);
    if (!mmio) {
        KdPrintf("[HDA] BAR0 not found\n");
        return;
    }
    VmmMapPages(VmmGetPml4(), PHYS_TO_VIRT(mmio & PAGE_MASK), mmio & PAGE_MASK, 32, VMM_KERNEL_FLAGS | VMM_NX);
    mmio = (u64)PHYS_TO_VIRT(mmio);
    g_hda.mmio_base = mmio;

    /* Enable PCI bus master, memory space, I/O space */
    u32 cmd_reg = PciReadConfig(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04);
    PciWriteConfig(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04, cmd_reg | 0x07);

    /* Disable PCIe No Snoop for cache coherence */
    HdaDisableNoSnoop();

    KdPrintf("[HDA] MMIO base=0x%llx\n", mmio);

    HdaResetController();

    u16 gcap = HdaRead16(g_hda.mmio_base, HDA_GCAP);
    KdPrintf("[HDA] GCAP=0x%04x oss=%u iss=%u bss=%u\n",
             gcap, (gcap >> 12) & 0xF, (gcap >> 8) & 0xF, (gcap >> 3) & 0x1F);

    /* CORB/RIRB not needed for init: we use Immediate Command (IC) interface
     * which avoids DMA cache-coherency issues in emulators. */
    /* HdaInitCorb(); */
    /* HdaInitRirb(); */

    if (HdaFindCodecAndPath() < 0) {
        KdPrintf("[HDA] Codec/path init failed\n");
        return;
    }

    HdaSetupAudioPath();

    g_hda.bdl_phys = PmmAllocPage();
    if (!g_hda.bdl_phys) {
        KdPrintf("[HDA] Failed to allocate BDL page\n");
        return;
    }
    g_hda.bdl = (volatile HdaBdlEntry*)PHYS_TO_VIRT(g_hda.bdl_phys);
    RtMemSet((void*)(usize)g_hda.bdl, 0, 4096);

    g_hda.pcm_size = 1024 * 1024;
    g_hda.pcm_phys = PmmAllocPages(g_hda.pcm_size / PAGE_SIZE);
    if (!g_hda.pcm_phys) {
        KdPrintf("[HDA] Failed to allocate PCM buffer\n");
        return;
    }
    RtMemSet((void*)PHYS_TO_VIRT(g_hda.pcm_phys), 0, g_hda.pcm_size);

    KdPrintf("[HDA] BDL=0x%llx PCM=0x%llx size=%u\n",
             g_hda.bdl_phys, g_hda.pcm_phys, g_hda.pcm_size);

    g_hda.initialized = 1;
    KdPrintf("[HDA] Initialized OK\n");
}

i32 HdaInitialized(void) {
    return g_hda.initialized;
}

ntstatus HdaPlayBuffer(const void* buffer, u32 size) {
    if (!g_hda.initialized) return STATUS_UNSUCCESSFUL;
    if (!buffer || size == 0 || size > g_hda.pcm_size) return STATUS_INVALID_PARAMETER;

    u64 sd_base = HdaGetOutputStreamBase(0);

    /* If already playing, stop the current stream immediately */
    if (g_hda.playing) {
        HdaStop();
    }

    u64 buf_phys = g_hda.pcm_phys;
    RtMemCopy((void*)PHYS_TO_VIRT(buf_phys), buffer, size);

    /* Flush PCM buffer from CPU cache so DMA can see the data */
    for (u32 i = 0; i < size; i += 64) {
        _mm_clflush((void*)PHYS_TO_VIRT(buf_phys + i));
    }
    _mm_mfence();

    g_hda.bdl[0].addr = buf_phys;
    g_hda.bdl[0].length = size;
    g_hda.bdl[0].ioc = 1;

    _mm_clflush((void*)PHYS_TO_VIRT(g_hda.bdl_phys));
    _mm_clflush((void*)PHYS_TO_VIRT(g_hda.bdl_phys + 64));
    _mm_mfence();

    /* Stop and reset stream */
    HdaWrite32(sd_base, HDA_SDn_CTL, HDA_SDn_CTL_SRST);
    u32 timeout = 10000;
    while (timeout-- && !(HdaRead32(sd_base, HDA_SDn_CTL) & HDA_SDn_CTL_SRST)) {
        _mm_pause();
    }
    HdaWrite32(sd_base, HDA_SDn_CTL, 0);
    timeout = 10000;
    while (timeout-- && (HdaRead32(sd_base, HDA_SDn_CTL) & HDA_SDn_CTL_SRST)) {
        _mm_pause();
    }

    /* Set stream ID=1, enable IOC */
    u32 ctl = (1 << HDA_SDn_CTL_STREAM_SHIFT)
            | HDA_SDn_CTL_IOCE;
    HdaWrite32(sd_base, HDA_SDn_CTL, ctl);

    /* 48kHz, 16-bit, stereo = 0x10011 */
    HdaWrite16(sd_base, HDA_SDn_FMT, (u16)0x10011);

    HdaWrite32(sd_base, HDA_SDn_CBL, size);
    HdaWrite16(sd_base, HDA_SDn_LVI, 0);

    HdaWrite32(sd_base, HDA_SDn_BDPL, (u32)(g_hda.bdl_phys & 0xFFFFFFFF));
    HdaWrite32(sd_base, HDA_SDn_BDPU, (u32)(g_hda.bdl_phys >> 32));

    /* Clear status */
    HdaWrite8(sd_base, HDA_SDn_STS, 0xFF);

    /* Start stream */
    ctl |= HDA_SDn_CTL_RUN;
    HdaWrite32(sd_base, HDA_SDn_CTL, ctl);

    /* Set async tracking info */
    g_hda.playing = 1;
    g_hda.bytes_to_play = size;
    g_hda.start_tick = KeGetTickCount();
    u32 duration_ms = (u32)((u64)size * 1000ULL / 192000ULL);
    g_hda.duration_ticks = (duration_ms / 10) + 2;

    return STATUS_SUCCESS;
}

void HdaStop(void) {
    if (!g_hda.initialized) return;
    u64 sd_base = HdaGetOutputStreamBase(0);

    u32 ctl_reg = HdaRead32(sd_base, HDA_SDn_CTL);
    HdaWrite32(sd_base, HDA_SDn_CTL, ctl_reg & ~HDA_SDn_CTL_RUN);

    u32 timeout = 10000;
    while (timeout-- && (HdaRead32(sd_base, HDA_SDn_CTL) & HDA_SDn_CTL_RUN)) {
        _mm_pause();
    }

    HdaWrite32(sd_base, HDA_SDn_CTL, HDA_SDn_CTL_SRST);
    timeout = 10000;
    while (timeout-- && !(HdaRead32(sd_base, HDA_SDn_CTL) & HDA_SDn_CTL_SRST)) {
        _mm_pause();
    }
    HdaWrite32(sd_base, HDA_SDn_CTL, 0);
    timeout = 10000;
    while (timeout-- && (HdaRead32(sd_base, HDA_SDn_CTL) & HDA_SDn_CTL_SRST)) {
        _mm_pause();
    }

    g_hda.playing = 0;
}

void HdaPoll(void) {
    if (!g_hda.initialized || !g_hda.playing) return;

    u64 sd_base = HdaGetOutputStreamBase(0);
    u32 lpib = HdaRead32(sd_base, HDA_SDn_LPIB);
    u8 sts = HdaRead8(sd_base, HDA_SDn_STS);

    int finished = 0;
    if (sts & HDA_SDn_STS_BCIS) {
        HdaWrite8(sd_base, HDA_SDn_STS, HDA_SDn_STS_BCIS);
        finished = 1;
    } else if (lpib >= g_hda.bytes_to_play) {
        finished = 1;
    } else if (KeGetTickCount() >= g_hda.start_tick + g_hda.duration_ticks) {
        finished = 1;
    }

    if (finished) {
        /* Stop stream */
        u32 ctl_reg = HdaRead32(sd_base, HDA_SDn_CTL);
        HdaWrite32(sd_base, HDA_SDn_CTL, ctl_reg & ~HDA_SDn_CTL_RUN);

        u32 run_stop = 1000;
        while (run_stop-- && (HdaRead32(sd_base, HDA_SDn_CTL) & HDA_SDn_CTL_RUN)) {
            _mm_pause();
        }

        HdaWrite32(sd_base, HDA_SDn_CTL, HDA_SDn_CTL_SRST);
        u32 rst_set = 1000;
        while (rst_set-- && !(HdaRead32(sd_base, HDA_SDn_CTL) & HDA_SDn_CTL_SRST)) {
            _mm_pause();
        }
        HdaWrite32(sd_base, HDA_SDn_CTL, 0);

        g_hda.playing = 0;
    }
}
