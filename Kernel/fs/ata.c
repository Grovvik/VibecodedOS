#include "ata.h"
#include "ahci.h"
#include "nvme.h"
#include "hal.h"
#include "debug.h"
#include "runtime.h"
#include "error.h"

#define ATA_PRIMARY_IO    0x1F0
#define ATA_PRIMARY_CTRL  0x3F6

#define ATA_REG_DATA     0
#define ATA_REG_FEATURES 1
#define ATA_REG_SECCOUNT 2
#define ATA_REG_LBA_LOW  3
#define ATA_REG_LBA_MID  4
#define ATA_REG_LBA_HIGH 5
#define ATA_REG_DRIVE    6
#define ATA_REG_STATUS   7
#define ATA_REG_COMMAND  7

#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_IDENTIFY      0xEC
#define ATA_CMD_FLUSH         0xE7

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_DRDY 0x40
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

static u16 g_ata_io_base;
static i32 g_ata_initialized;

static u8 AtaReadStatus(void) {
    return HalInByte(g_ata_io_base + ATA_REG_STATUS);
}

static i32 AtaWaitForBsy(void) {
    i32 timeout = 100000;
    while ((AtaReadStatus() & ATA_STATUS_BSY) && timeout > 0) {
        timeout--;
        HalIoWait();
    }
    return timeout > 0 ? 0 : -1;
}

static i32 AtaWaitForDrq(void) {
    i32 timeout = 100000;
    while (!((AtaReadStatus() & ATA_STATUS_DRQ)) && timeout > 0) {
        timeout--;
        HalIoWait();
    }
    return timeout > 0 ? 0 : -1;
}

void AtaInit(void) {
    g_ata_io_base = ATA_PRIMARY_IO;

    HalOutByte(ATA_PRIMARY_CTRL, 0x00);
    HalIoWait();

    HalOutByte(g_ata_io_base + ATA_REG_DRIVE, 0xA0);
    HalIoWait();

    u8 status = AtaReadStatus();
    if (status == 0xFF || status == 0x00) {
        KdPrintf("[ATA] No primary master detected (status=0x%02x)\n", status);
        g_ata_initialized = 0;
        return;
    }

    if (AtaWaitForBsy() < 0) {
        KdPrintf("[ATA] BSY timeout during init\n");
        g_ata_initialized = 0;
        return;
    }

    g_ata_initialized = 1;
    KdPrintf("[ATA] Primary master detected (status=0x%02x)\n", status);
}

static ntstatus AtaLegacyReadSectors(u64 lba, u64 count, void* buffer) {
    if (!g_ata_initialized) return STATUS_UNSUCCESSFUL;
    if (count == 0 || count > 256) return STATUS_INVALID_PARAMETER;

    u8* dst = (u8*)buffer;

    for (u64 sec = 0; sec < count; sec++) {
        u64 cur_lba = lba + sec;

        if (AtaWaitForBsy() < 0) return STATUS_UNSUCCESSFUL;

        HalOutByte(g_ata_io_base + ATA_REG_DRIVE, 0xE0 | ((cur_lba >> 24) & 0x0F));
        HalIoWait();

        HalOutByte(g_ata_io_base + ATA_REG_FEATURES, 0x00);
        HalOutByte(g_ata_io_base + ATA_REG_SECCOUNT, 1);
        HalOutByte(g_ata_io_base + ATA_REG_LBA_LOW, (u8)(cur_lba & 0xFF));
        HalOutByte(g_ata_io_base + ATA_REG_LBA_MID, (u8)((cur_lba >> 8) & 0xFF));
        HalOutByte(g_ata_io_base + ATA_REG_LBA_HIGH, (u8)((cur_lba >> 16) & 0xFF));

        HalOutByte(g_ata_io_base + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

        if (AtaWaitForBsy() < 0) return STATUS_UNSUCCESSFUL;

        u8 status = AtaReadStatus();
        if (status & ATA_STATUS_ERR) {
            KdPrintf("[ATA] Read error at LBA %llu (status=0x%02x)\n", cur_lba, status);
            return STATUS_UNSUCCESSFUL;
        }

        if (AtaWaitForDrq() < 0) return STATUS_UNSUCCESSFUL;

        u16* p = (u16*)(dst + sec * ATA_SECTOR_SIZE);
        for (i32 i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
            p[i] = HalInWord(g_ata_io_base + ATA_REG_DATA);
        }
    }

    return STATUS_SUCCESS;
}

static ntstatus AtaLegacyWriteSectors(u64 lba, u64 count, const void* buffer) {
    if (!g_ata_initialized) return STATUS_UNSUCCESSFUL;
    if (count == 0 || count > 256) return STATUS_INVALID_PARAMETER;

    const u8* src = (const u8*)buffer;

    for (u64 sec = 0; sec < count; sec++) {
        u64 cur_lba = lba + sec;

        if (AtaWaitForBsy() < 0) return STATUS_UNSUCCESSFUL;

        HalOutByte(g_ata_io_base + ATA_REG_DRIVE, 0xE0 | ((cur_lba >> 24) & 0x0F));
        HalIoWait();

        HalOutByte(g_ata_io_base + ATA_REG_FEATURES, 0x00);
        HalOutByte(g_ata_io_base + ATA_REG_SECCOUNT, 1);
        HalOutByte(g_ata_io_base + ATA_REG_LBA_LOW, (u8)(cur_lba & 0xFF));
        HalOutByte(g_ata_io_base + ATA_REG_LBA_MID, (u8)((cur_lba >> 8) & 0xFF));
        HalOutByte(g_ata_io_base + ATA_REG_LBA_HIGH, (u8)((cur_lba >> 16) & 0xFF));

        HalOutByte(g_ata_io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);

        if (AtaWaitForBsy() < 0) return STATUS_UNSUCCESSFUL;
        if (AtaWaitForDrq() < 0) return STATUS_UNSUCCESSFUL;

        const u16* p = (const u16*)(src + sec * ATA_SECTOR_SIZE);
        for (i32 i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
            HalOutWord(g_ata_io_base + ATA_REG_DATA, p[i]);
        }

        if (AtaWaitForBsy() < 0) return STATUS_UNSUCCESSFUL;

        HalOutByte(g_ata_io_base + ATA_REG_COMMAND, ATA_CMD_FLUSH);
        if (AtaWaitForBsy() < 0) return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

ntstatus AtaReadSectors(u64 lba, u64 count, void* buffer) {
    if (AhciInitialized()) return AhciReadSectors(lba, count, buffer);
    if (NvmeInitialized()) return NvmeReadSectors(lba, count, buffer);
    return AtaLegacyReadSectors(lba, count, buffer);
}

ntstatus AtaWriteSectors(u64 lba, u64 count, const void* buffer) {
    if (AhciInitialized()) return AhciWriteSectors(lba, count, buffer);
    if (NvmeInitialized()) return NvmeWriteSectors(lba, count, buffer);
    return AtaLegacyWriteSectors(lba, count, buffer);
}
