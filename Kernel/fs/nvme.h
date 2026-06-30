#ifndef _KERNEL_FS_NVME_H_
#define _KERNEL_FS_NVME_H_

#include "types.h"

void NvmeInit(void);
i32  NvmeInitialized(void);
ntstatus NvmeReadSectors(u64 lba, u64 count, void* buffer);
ntstatus NvmeWriteSectors(u64 lba, u64 count, const void* buffer);

#endif
