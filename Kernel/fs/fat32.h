#ifndef _KERNEL_FS_FAT32_H_
#define _KERNEL_FS_FAT32_H_

#include "types.h"
#include "error.h"

#define FAT_MAX_PATH     256
#define FAT_MAX_ENTRIES  512
#define FAT_NAME_LEN    11

typedef enum {
    FAT_TYPE_12,
    FAT_TYPE_16,
    FAT_TYPE_32
} FatType;

typedef struct {
    char  name[256];
    u32   first_cluster;
    u32   file_size;
    u8    is_directory;
    u8    is_readonly;
    u8    is_hidden;
    u8    is_system;
    u8    is_volume_label;
    u8    is_long_name;
    u32   attributes;
} FatDirEntry;

typedef struct {
    FatType type;
    u32 bytes_per_sector;
    u32 sectors_per_cluster;
    u32 reserved_sectors;
    u32 num_fats;
    u32 root_entry_count;
    u32 root_cluster;
    u32 sectors_per_fat;
    u32 total_sectors;
    u32 data_start_sector;
    u32 fat_start_sector;
    u32 root_dir_sectors;
    u32 root_dir_start_sector;
    u64 partition_start_lba;
} FatInfo;

void      Fat32Init(u64 partition_start_lba);
ntstatus  Fat32OpenRoot(void);
ntstatus  Fat32OpenPath(const char* path);
ntstatus  Fat32OpenDir(const char* name);
ntstatus  Fat32ReadDir(FatDirEntry* entries, u32* count);
ntstatus  Fat32ReadFile(void* buffer, u32* bytes_read);
u32       Fat32GetFileSize(void);
void      Fat32Close(void);
u8        Fat32IsCurrentDirectory(void);
ntstatus  Fat32ReadFileAt(const char* path, u32 offset, void* buffer, u32 count, u32* bytes_read);
ntstatus  Fat32WriteFile(const char* path, const void* data, u32 size);
ntstatus  Fat32DeleteFile(const char* path);
ntstatus  Fat32CreateDirectory(const char* path);
u64       Fat32GetVolumeSize(void);
u64       Fat32GetFreeSize(void);

#endif
