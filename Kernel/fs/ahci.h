#ifndef _KERNEL_FS_AHCI_H_
#define _KERNEL_FS_AHCI_H_

#include "types.h"

void AhciInit(void);
i32  AhciInitialized(void);
ntstatus AhciReadSectors(u64 lba, u64 count, void* buffer);
ntstatus AhciWriteSectors(u64 lba, u64 count, const void* buffer);

#endif
