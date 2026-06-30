#ifndef _KERNEL_FS_ATA_H_
#define _KERNEL_FS_ATA_H_

#include "types.h"

#define ATA_SECTOR_SIZE 512

void AtaInit(void);
ntstatus AtaReadSectors(u64 lba, u64 count, void* buffer);
ntstatus AtaWriteSectors(u64 lba, u64 count, const void* buffer);

#endif