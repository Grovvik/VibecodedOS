#include "nvme.h"
#include "pci.h"
#include "hal.h"
#include "debug.h"
#include "runtime.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "error.h"
#include <intrin.h>
#include "pit.h"

#define NVME_CAP       0x00
#define NVME_VS        0x08
#define NVME_INTMC     0x10
#define NVME_CC        0x14
#define NVME_CSTS      0x1C
#define NVME_AQA       0x24
#define NVME_ASQ       0x28
#define NVME_ACQ       0x30

#define NVME_CC_EN      0x00000001
#define NVME_CC_IOSQES 0x00000070
#define NVME_CC_IOCQES 0x00000700

#define NVME_CSTS_RDY  0x00000001
#define NVME_CSTS_CFS  0x00000002

#define NVME_CAP_MQES(cap)   ((cap) & 0xFFFF)
#define NVME_CAP_DSTRD(cap)  (((cap) >> 32) & 0xF)

#define NVME_OPC_READ     0x02
#define NVME_OPC_WRITE    0x01

#define NVME_ADMIN_OPC_CREATE_SQ  0x01
#define NVME_ADMIN_OPC_CREATE_CQ  0x05
#define NVME_ADMIN_OPC_IDENTIFY   0x06

#define NVME_CNS_CONTROLLER 0x01
#define NVME_CNS_NAMESPACE  0x00

#define NVME_SQ_PRIO_MEDIUM 0x01

#define MemoryBarrier() _ReadWriteBarrier(); _mm_mfence();

#pragma pack(push, 1)

typedef struct {
    u32 cdw0;      // 0..3
    u32 nsid;      // 4..7
    u64 rsvd1;     // 8..15   rsvd1 + rsvd2
    u64 mptr;      // 16..23  metadata
    u64 prp1;      // 24..31  base DMA buffer
    u64 prp2;      // 32..39  optional DMA buffer
    u32 cdw10;     // 40..43
    u32 cdw11;     // 44..47
    u32 cdw12;     // 48..51
    u32 cdw13;     // 52..55
    u32 cdw14;     // 56..59
    u32 cdw15;     // 60..63
} NvmeCmd;

typedef struct {
    u32 cdw0;
    u32 rsvd1;
    u16 sqhd;
    u16 sqid;
    u16 cid;
    u16 status;
} NvmeCqe;

#pragma pack(pop)

typedef struct {
    u64 mmio_base;
    u32 doorbell_stride;
    u16 asq_size;
    u16 acq_size;
    u64 asq_phys;
    u64 acq_phys;
    volatile NvmeCmd* asq;
    volatile NvmeCqe* acq;
    u32 asq_tail;
    u32 acq_head;
    u8  acq_phase;
    u16 next_cid;
    u64 iosq_phys;
    u64 iocq_phys;
    volatile NvmeCmd* iosq;
    volatile NvmeCqe* iocq;
    u32 iosq_tail;
    u32 iocq_head;
    u8  iocq_phase;
    u16 sqid_io;
    u32 nsid;
    u64 ns_sectors;
    u32 ns_lba_size;
    u8  initialized;
} NvmeController;

static NvmeController g_nvme;

static u32 NvmeRead32(u64 base, u32 offset) {
    return *(volatile u32*)(usize)(base + offset);
}

static void NvmeWrite32(u64 base, u32 offset, u32 value) {
    *(volatile u32*)(usize)(base + offset) = value;
}

static u64 NvmeRead64(u64 base, u32 offset) {
    return *(volatile u64*)(usize)(base + offset);
}

static void NvmeWrite64(u64 base, u32 offset, u64 value) {
    *(volatile u64*)(usize)(base + offset) = value;
}

static void NvmeRingSqDoorbell(u16 sqid, u32 tail) {
    u32 stride_bytes = 4 << g_nvme.doorbell_stride;
    u32 offset = 0x1000 + (2 * (u32)sqid) * stride_bytes;
    NvmeWrite32(g_nvme.mmio_base, offset, tail);
}

static void NvmeRingCqDoorbell(u16 cqid, u32 head) {
    u32 stride_bytes = 4 << g_nvme.doorbell_stride;
    u32 offset = 0x1000 + (2 * (u32)cqid + 1) * stride_bytes;
    NvmeWrite32(g_nvme.mmio_base, offset, head);
}

static u16 NvmeSubmitAdminCmd(NvmeCmd* cmd) {
    u16 cid = ++g_nvme.next_cid;
    cmd->cdw0 = (cmd->cdw0 & 0x0000FFFF) | ((u32)cid << 16);

    g_nvme.asq[g_nvme.asq_tail] = *cmd;
    u32 tail = (g_nvme.asq_tail + 1) % g_nvme.asq_size;
    g_nvme.asq_tail = tail;

    NvmeRingSqDoorbell(0, tail);
    return cid;
}

static ntstatus NvmeWaitAdminCq(u16 expected_cid) {
    u32 timeout = 5000000;
    while (timeout--) {
        MemoryBarrier();

        NvmeCqe cqe = g_nvme.acq[g_nvme.acq_head];
        u8 phase = cqe.status & 0x01;

        if (phase == g_nvme.acq_phase) {
            u32 new_head = (g_nvme.acq_head + 1) % g_nvme.acq_size;
            if (new_head == 0)
                g_nvme.acq_phase ^= 1;

            g_nvme.acq_head = new_head;
            NvmeRingCqDoorbell(0, new_head);

            if (cqe.cid == expected_cid) {
                u16 status_filed = cqe.status >> 1; // Убираем бит фазы
                u8 sc = (u8)(status_filed & 0xFF);
                u8 sct = (u8)((status_filed >> 8) & 0x07);

                if (sc != 0 || sct != 0) {
                    KdPrintf("[NVMe] Admin Error: SC=%u SCT=%u\n", sc, sct);
                    return STATUS_UNSUCCESSFUL;
                }
                return STATUS_SUCCESS;
            }
        }
    }
    KdPrintf("[NVMe] Admin CQ timeout (head=%u phase=%u)\n", g_nvme.acq_head, g_nvme.acq_phase);
    return STATUS_UNSUCCESSFUL;
}

static u16 NvmeSubmitIoCmd(NvmeCmd* cmd) {
    u16 cid = ++g_nvme.next_cid;
    cmd->cdw0 = (cmd->cdw0 & 0x0000FFFF) | ((u32)cid << 16);

    u32 max_io_sq_entries = 4096 / sizeof(NvmeCmd);

    g_nvme.iosq[g_nvme.iosq_tail] = *cmd;

    u32 tail = (g_nvme.iosq_tail + 1) % max_io_sq_entries;
    g_nvme.iosq_tail = tail;

    NvmeRingSqDoorbell(g_nvme.sqid_io, tail);
    return cid;
}

static ntstatus NvmeWaitIoCq(u16 expected_cid) {
    u32 timeout = 5000000;
    u32 max_io_cq_entries = 4096 / sizeof(NvmeCqe);

    while (timeout--) {
        MemoryBarrier();

        NvmeCqe cqe = g_nvme.iocq[g_nvme.iocq_head];
        u8 phase = cqe.status & 0x01;

        if (phase == g_nvme.iocq_phase) {
            u32 new_head = (g_nvme.iocq_head + 1) % max_io_cq_entries;
            if (new_head == 0)
                g_nvme.iocq_phase ^= 1;

            g_nvme.iocq_head = new_head;
            NvmeRingCqDoorbell(1, new_head);

            if (cqe.cid == expected_cid) {
                u16 status_filed = cqe.status >> 1;
                u8 sc = (u8)(status_filed & 0xFF);
                if (sc != 0) {
                    KdPrintf("[NVMe] IO Error: SC=%u\n", sc);
                    return STATUS_UNSUCCESSFUL;
                }
                return STATUS_SUCCESS;
            }
        }
    }
    KdPrintf("[NVMe] IO CQ timeout (want cid=%u)\n", expected_cid);
    return STATUS_UNSUCCESSFUL;
}

static ntstatus NvmeAdminIdentify(u32 nsid, u8 cns, void* buffer) {
    u64 buf_phys = PmmAllocPage();
    if (!buf_phys) return STATUS_OUT_OF_MEMORY;

    RtMemSet((void*)PHYS_TO_VIRT(buf_phys), 0, 4096);

    NvmeCmd cmd;
    RtMemSet(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_ADMIN_OPC_IDENTIFY;
    cmd.nsid = nsid;
    cmd.prp1 = buf_phys;
    cmd.cdw10 = (u32)cns;

    u16 cid = NvmeSubmitAdminCmd(&cmd);
    ntstatus status = NvmeWaitAdminCq(cid);

    if (NT_SUCCESS(status)) {
        RtMemCopy(buffer, (void*)PHYS_TO_VIRT(buf_phys), 4096);
    }

    PmmFreePage(buf_phys);
    return status;
}

static ntstatus NvmeCreateIoQueues(void) {
    u16 cqid = 1;
    u16 sqid = 1;
    g_nvme.sqid_io = sqid;

    // Размер в терминах NVMe — это "количество слотов минус 1"
    u16 cq_size = (u16)(4096 / sizeof(NvmeCqe));
    u16 sq_size = (u16)(4096 / sizeof(NvmeCmd));

    NvmeCmd cmd;

    // 1. Создание IO Completion Queue (CQ)
    RtMemSet(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_ADMIN_OPC_CREATE_CQ;
    cmd.prp1 = g_nvme.iocq_phys;
    cmd.cdw10 = ((u32)cqid) | (((u32)cq_size - 1) << 16);
    cmd.cdw11 = 0x00000001; // Бит 0 (PC) = 1 (Физически непрерывна), Прерывания отключены (0)

    u16 cid = NvmeSubmitAdminCmd(&cmd);
    ntstatus status = NvmeWaitAdminCq(cid);
    if (NT_ERROR(status)) {
        KdPrintf("[NVMe] Create IO CQ failed\n");
        return status;
    }

    RtMemSet(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_ADMIN_OPC_CREATE_SQ;
    cmd.prp1 = g_nvme.iosq_phys;
    cmd.cdw10 = ((u32)sqid) | (((u32)sq_size - 1) << 16);

    cmd.cdw11 = 0x00000001 | ((u32)cqid << 16);

    cid = NvmeSubmitAdminCmd(&cmd);
    status = NvmeWaitAdminCq(cid);
    if (NT_ERROR(status)) {
        KdPrintf("[NVMe] Create IO SQ failed\n");
        return status;
    }

    return STATUS_SUCCESS;
}

static u64 NvmeAllocDmaPage(void) {
    u64 low_pages[128];
    i32 low_count = 0;
    u64 found_page = 0;

    for (i32 retry = 0; retry < 128; retry++) {
        u64 page = PmmAllocPage();
        if (!page) break;

        if (page >= 0x100000) {
            found_page = page;
            break;
        }

        if (low_count < 128) {
            low_pages[low_count++] = page;
        }
        else {
            PmmFreePage(page);
        }
    }

    for (i32 i = 0; i < low_count; i++) {
        PmmFreePage(low_pages[i]);
    }

    return found_page ? found_page : PmmAllocPage();
}

static i32 NvmeInitFresh(u64 mmio) {
    KdPrintf("[NVMe] Performing fresh init (disable/enable)\n");

    NvmeWrite32(mmio, NVME_CC, 0);
    u32 timeout = 2000000;
    while (timeout-- && (NvmeRead32(mmio, NVME_CSTS) & NVME_CSTS_RDY));
    if (NvmeRead32(mmio, NVME_CSTS) & NVME_CSTS_RDY) {
        KdPrintf("[NVMe] Disable timeout\n");
        return -1;
    }

    g_nvme.asq_phys = NvmeAllocDmaPage();
    g_nvme.acq_phys = NvmeAllocDmaPage();
    if (!g_nvme.asq_phys || !g_nvme.acq_phys) {
        KdPrintf("[NVMe] Failed to allocate admin queues\n");
        return -1;
    }

    g_nvme.asq = (NvmeCmd*)PHYS_TO_VIRT(g_nvme.asq_phys);
    g_nvme.acq = (NvmeCqe*)PHYS_TO_VIRT(g_nvme.acq_phys);
    RtMemSet(g_nvme.asq, 0, 4096);
    RtMemSet(g_nvme.acq, 0, 4096);
    g_nvme.asq_tail = 0;
    g_nvme.acq_head = 0;
    g_nvme.acq_phase = 1;
    g_nvme.next_cid = 0;
    g_nvme.asq_size = 4096 / sizeof(NvmeCmd);
    g_nvme.acq_size = 4096 / sizeof(NvmeCqe);
    u64 cap = NvmeRead64(mmio, NVME_CAP);
    g_nvme.doorbell_stride = NVME_CAP_DSTRD(cap);

    KdPrintf("[NVMe] ASQ=0x%llx ACQ=0x%llx (sq_size=%u cq_size=%u)\n",
        g_nvme.asq_phys, g_nvme.acq_phys, g_nvme.asq_size, g_nvme.acq_size);

    NvmeWrite32(mmio, NVME_AQA, ((u32)(g_nvme.asq_size - 1)) | (((u32)(g_nvme.acq_size - 1)) << 16));
    NvmeWrite64(mmio, NVME_ASQ, g_nvme.asq_phys);
    NvmeWrite64(mmio, NVME_ACQ, g_nvme.acq_phys);

    u32 intmc = NvmeRead32(mmio, NVME_INTMC);
    NvmeWrite32(mmio, NVME_INTMC, intmc);

    u32 cc = NVME_CC_EN;
    cc |= (6 << 16);
    cc |= (4 << 20);
    NvmeWrite32(mmio, NVME_CC, cc);

    timeout = 5000000;
    while (timeout--) {
        if (NvmeRead32(mmio, NVME_CSTS) & NVME_CSTS_RDY) break;
    }
    u32 csts = NvmeRead32(mmio, NVME_CSTS);
    if (!(csts & NVME_CSTS_RDY)) {
        KdPrintf("[NVMe] Enable timeout CSTS=0x%08x\n", csts);
        return -1;
    }

    KdPrintf("[NVMe] Controller enabled CSTS=0x%08x\n", csts);
    return 0;
}

static i32 NvmeInitReuseUefi(u64 mmio) {
    KdPrintf("[NVMe] Reusing UEFI queues\n");

    u32 aqa = NvmeRead32(mmio, NVME_AQA);
    g_nvme.asq_size = (u16)((aqa & 0xFFF) + 1);
    g_nvme.acq_size = (u16)(((aqa >> 16) & 0xFFF) + 1);

    g_nvme.asq_phys = NvmeRead64(mmio, NVME_ASQ) & ~0xFFFULL;
    g_nvme.acq_phys = NvmeRead64(mmio, NVME_ACQ) & ~0xFFFULL;

    KdPrintf("[NVMe] AQA=0x%08x asq_size=%u acq_size=%u\n", aqa, g_nvme.asq_size, g_nvme.acq_size);
    KdPrintf("[NVMe] ASQ=0x%llx ACQ=0x%llx\n", g_nvme.asq_phys, g_nvme.acq_phys);

    u64* pml4 = VmmGetPml4();
    u64 asq_bytes = (u64)g_nvme.asq_size * sizeof(NvmeCmd);
    u64 acq_bytes = (u64)g_nvme.acq_size * sizeof(NvmeCqe);
    VmmIdentityMapRange(pml4, g_nvme.asq_phys, asq_bytes, VMM_KERNEL_FLAGS);
    VmmIdentityMapRange(pml4, g_nvme.acq_phys, acq_bytes, VMM_KERNEL_FLAGS);

    g_nvme.asq = (NvmeCmd*)PHYS_TO_VIRT(g_nvme.asq_phys);
    g_nvme.acq = (NvmeCqe*)PHYS_TO_VIRT(g_nvme.acq_phys);

    g_nvme.asq_tail = 0;
    for (u32 i = 0; i < g_nvme.asq_size; i++) {
        if (g_nvme.asq[i].cdw0 != 0) {
            g_nvme.asq_tail = (i + 1) % g_nvme.asq_size;
        }
    }

    KdPrintf("[NVMe] ASQ scan: tail=%u (last nonzero cmd index)\n", g_nvme.asq_tail);

    u32 consumed = 0;
    u8 last_phase = 0;
    g_nvme.acq_head = 0;
    g_nvme.acq_phase = 0;

    for (u32 i = 0; i < g_nvme.acq_size; i++) {
        NvmeCqe cqe = g_nvme.acq[i];
        u8 phase = cqe.status & 0x01;
        if (cqe.cdw0 != 0 || cqe.status != 0) {
            last_phase = phase;
            consumed++;
        }
    }

    if (consumed > 0) {
        u32 last_valid = 0;
        for (u32 i = 0; i < g_nvme.acq_size; i++) {
            if (g_nvme.acq[i].cdw0 != 0 || g_nvme.acq[i].status != 0)
                last_valid = i;
        }

        g_nvme.acq_phase = last_phase;
        if (last_valid + 1 >= g_nvme.acq_size)
            g_nvme.acq_phase ^= 1;
        g_nvme.acq_head = (last_valid + 1) % g_nvme.acq_size;
    }
    else {
        g_nvme.acq_head = 0;
        g_nvme.acq_phase = 1;
    }

    NvmeRingCqDoorbell(0, g_nvme.acq_head);

    g_nvme.next_cid = 0;

    KdPrintf("[NVMe] Synced: sq_tail=%u cq_head=%u phase=%u\n",
        g_nvme.asq_tail, g_nvme.acq_head, g_nvme.acq_phase);
    KdPrintf("[NVMe] acq[head=%u].status=0x%04x\n",
        g_nvme.acq_head, g_nvme.acq[g_nvme.acq_head].status);

    return 0;
}

void NvmeInit(void) {
    RtMemSet(&g_nvme, 0, sizeof(g_nvme));

    PciDevice pci_dev;
    ntstatus status = PciFindDevice(&pci_dev, 0x01, 0x08, 0x02);
    if (NT_ERROR(status)) {
        KdPrintf("[NVMe] No NVMe controller found\n");
        return;
    }

    u64 mmio = PciGetBarAddress(pci_dev.bus, pci_dev.device, pci_dev.function, 0);
    if (!mmio) return;
    VmmMapPages(VmmGetPml4(), PHYS_TO_VIRT(mmio & PAGE_MASK), mmio & PAGE_MASK, 32, VMM_KERNEL_FLAGS | VMM_NX);
    mmio = (u64)PHYS_TO_VIRT(mmio);
    g_nvme.mmio_base = mmio;

    u32 cmd_reg = PciReadConfig(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04);
    PciWriteConfig(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04, cmd_reg | 0x07);

    KdPrintf("[NVMe] Performing Hard Reset\n");

    NvmeWrite32(mmio, NVME_CC, 0);

    u32 timeout = 1000000;
    while (timeout-- && (NvmeRead32(mmio, NVME_CSTS) & NVME_CSTS_RDY));

    if (NvmeInitFresh(mmio) < 0) {
        KdPrintf("[NVMe] Fresh init failed\n");
        return;
    }

    u8* identify_ctrl = (u8*)KmAlloc(4096);
    if (!identify_ctrl) { KdPrintf("[NVMe] OOM\n"); return; }
    status = NvmeAdminIdentify(0, NVME_CNS_CONTROLLER, identify_ctrl);
    if (NT_ERROR(status)) {
        KdPrintf("[NVMe] Identify controller failed\n");
        KmFree(identify_ctrl);
        return;
    }

    u32 nn = *(u32*)(identify_ctrl + 516);
    KdPrintf("[NVMe] data[0:16]=");
    for (i32 i = 0; i < 16; i++) KdPrintf("%02x", identify_ctrl[i]);
    KdPrintf(" NN=%u\n", nn);
    KmFree(identify_ctrl);

    if (nn == 0) {
        KdPrintf("[NVMe] NN=0, trying NSID=1 anyway\n");
    }

    g_nvme.nsid = 1;
    u8* identify_ns = (u8*)KmAlloc(4096);
    if (!identify_ns) { KdPrintf("[NVMe] OOM\n"); return; }
    status = NvmeAdminIdentify(g_nvme.nsid, NVME_CNS_NAMESPACE, identify_ns);
    if (NT_ERROR(status)) {
        KdPrintf("[NVMe] Identify NS 1 failed\n");
        KmFree(identify_ns);
        return;
    }

    u64 nsze = *(u64*)(identify_ns + 0);
    u64 ncap = *(u64*)(identify_ns + 8);
    u8 lbaf = identify_ns[26] & 0x3F;
    u32 lba_ds = identify_ns[128 + lbaf * 4];
    if (lba_ds == 0) lba_ds = 9;
    u32 sector_size = 1 << lba_ds;

    g_nvme.ns_sectors = nsze;
    g_nvme.ns_lba_size = sector_size;

    KdPrintf("[NVMe] NS 1: %llu sectors, LBA=%u, cap=%llu\n", nsze, sector_size, ncap);
    KmFree(identify_ns);

    if (nsze == 0) {
        KdPrintf("[NVMe] Namespace 1 empty\n");
        return;
    }

    g_nvme.iosq_phys = NvmeAllocDmaPage();
    g_nvme.iocq_phys = NvmeAllocDmaPage();
    if (!g_nvme.iosq_phys || !g_nvme.iocq_phys) {
        KdPrintf("[NVMe] Failed to allocate IO queues\n");
        return;
    }

    g_nvme.iosq = (NvmeCmd*)PHYS_TO_VIRT(g_nvme.iosq_phys);
    g_nvme.iocq = (NvmeCqe*)PHYS_TO_VIRT(g_nvme.iocq_phys);
    RtMemSet(g_nvme.iosq, 0, 4096);
    RtMemSet(g_nvme.iocq, 0, 4096);
    g_nvme.iosq_tail = 0;
    g_nvme.iocq_head = 0;
    g_nvme.iocq_phase = 1;

    status = NvmeCreateIoQueues();
    if (NT_ERROR(status)) {
        KdPrintf("[NVMe] IO queue creation failed\n");
        return;
    }

    g_nvme.initialized = 1;
    KdPrintf("[NVMe] Initialized OK, %llu MB\n", g_nvme.ns_sectors * g_nvme.ns_lba_size / (1024 * 1024));
}

i32 NvmeInitialized(void) {
    return g_nvme.initialized;
}

ntstatus NvmeReadSectors(u64 lba, u64 count, void* buffer) {
    if (!g_nvme.initialized) return STATUS_UNSUCCESSFUL;
    if (count == 0) return STATUS_INVALID_PARAMETER;

    u8* dst = (u8*)buffer;
    u32 sector_size = g_nvme.ns_lba_size;

    for (u64 sec = 0; sec < count; sec++) {
        u64 buf_phys = NvmeAllocDmaPage();
        if (!buf_phys) return STATUS_OUT_OF_MEMORY;

        NvmeCmd cmd;
        RtMemSet(&cmd, 0, sizeof(cmd));
        cmd.cdw0 = NVME_OPC_READ;
        cmd.nsid = g_nvme.nsid;
        cmd.prp1 = buf_phys;
        u64 cur_lba = lba + sec;
        cmd.cdw10 = (u32)(cur_lba & 0xFFFFFFFF);
        cmd.cdw11 = (u32)(cur_lba >> 32);
        cmd.cdw12 = 0;

        u16 cid = NvmeSubmitIoCmd(&cmd);
        ntstatus status = NvmeWaitIoCq(cid);

        if (NT_SUCCESS(status)) {
            RtMemCopy(dst + sec * sector_size, (void*)PHYS_TO_VIRT(buf_phys), sector_size);
        }

        PmmFreePage(buf_phys);

        if (NT_ERROR(status)) return status;
    }

    return STATUS_SUCCESS;
}

ntstatus NvmeWriteSectors(u64 lba, u64 count, const void* buffer) {
    if (!g_nvme.initialized) return STATUS_UNSUCCESSFUL;
    if (count == 0) return STATUS_INVALID_PARAMETER;

    const u8* src = (const u8*)buffer;
    u32 sector_size = g_nvme.ns_lba_size;

    for (u64 sec = 0; sec < count; sec++) {
        u64 buf_phys = NvmeAllocDmaPage();
        if (!buf_phys) return STATUS_OUT_OF_MEMORY;

        RtMemCopy((void*)PHYS_TO_VIRT(buf_phys), src + sec * sector_size, sector_size);

        NvmeCmd cmd;
        RtMemSet(&cmd, 0, sizeof(cmd));
        cmd.cdw0 = NVME_OPC_WRITE;
        cmd.nsid = g_nvme.nsid;
        cmd.prp1 = buf_phys;
        u64 cur_lba = lba + sec;
        cmd.cdw10 = (u32)(cur_lba & 0xFFFFFFFF);
        cmd.cdw11 = (u32)(cur_lba >> 32);
        cmd.cdw12 = 0;

        u16 cid = NvmeSubmitIoCmd(&cmd);
        ntstatus status = NvmeWaitIoCq(cid);

        PmmFreePage(buf_phys);

        if (NT_ERROR(status)) return status;
    }

    return STATUS_SUCCESS;
}