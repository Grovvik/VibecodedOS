#ifndef _SHARED_BOOT_INFO_H_
#define _SHARED_BOOT_INFO_H_

#include "types.h"

typedef struct {
    u64 physical_start;
    u64 virtual_start;
    u64 number_of_pages;
    u64 type;
    u64 attributes;
} MemoryMapEntry;

#define MEMORY_TYPE_USABLE              7
#define MEMORY_TYPE_BOOT_DATA           4
#define MEMORY_TYPE_RUNTIME_CODE        5
#define MEMORY_TYPE_RUNTIME_DATA        6
#define MEMORY_TYPE_UNUSABLE            2
#define MEMORY_TYPE_ACPI_RECLAIM        3
#define MEMORY_TYPE_PERSISTENT          11

#define MAX_DISK_PARTITIONS 4

typedef struct {
    u64 start_lba;
    u64 sector_count;
    u8  type;
    u8  active;
    u8  reserved[6];
} PartitionInfo;

typedef struct {
    u64 disk_size_sectors;
    u32 sector_size;
    u32 heads;
    u32 sectors_per_track;
    u32 cylinders;
    u8  disk_type;
    u8  num_partitions;
    u8  reserved[6];
    PartitionInfo partitions[MAX_DISK_PARTITIONS];
} DiskInfo;

typedef struct {
    void*    memory_map;
    u64      memory_map_size;
    u64      memory_map_entry_size;
    u64      memory_map_entry_count;

    u64      fb_base;
    u64      fb_size;
    u32      fb_width;
    u32      fb_height;
    u32      fb_pitch;
    u32      fb_bpp;

    u64      rsdp;

    u64      kernel_image_base;
    u64      kernel_entry_point;
    u64      kernel_image_size;

    u64      user_program_base;
    u64      user_program_size;

    u64      total_usable_memory;

    u8       boot_drive;
    u8       reserved1[7];

    DiskInfo disk;
} BootInfo;

#endif
