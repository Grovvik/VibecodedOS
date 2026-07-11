#include "fat32.h"
#include "ata.h"
#include "debug.h"
#include "runtime.h"
#include "heap.h"
#include "error.h"

#define FAT_ATTR_READONLY   0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F

#pragma pack(push, 1)

typedef struct {
    u64 signature; // "EFI PART" (0x5452415020494645)
    u32 revision;
    u32 header_size;
    u32 header_crc32;
    u32 reserved;
    u64 current_lba;
    u64 backup_lba;
    u64 first_usable_lba;
    u64 last_usable_lba;
    u8  disk_guid[16];
    u64 partition_entry_lba;
    u32 num_partition_entries;
    u32 size_of_partition_entry;
    u32 partition_entry_array_crc32;
} GptHeader;

typedef struct {
    u8  type_guid[16];
    u8  unique_guid[16];
    u64 starting_lba;
    u64 ending_lba;
    u64 attributes;
    u16 name[36];
} GptEntry;

#pragma pack(pop)

static FatInfo g_fat;
static u32     g_current_cluster;
static u32     g_current_file_size;
static u8      g_current_is_directory;
static i32     g_fat_initialized;

static void FatGenerateShortName(const char* long_name, char* short_name);

static u8* g_sector_buf;

static ntstatus FatReadSector(u32 sector, void* buffer) {
    u64 lba = g_fat.partition_start_lba + sector;
    return AtaReadSectors(lba, 1, buffer);
}

static ntstatus FatWriteSector(u32 sector, const void* buffer) {
    u64 lba = g_fat.partition_start_lba + sector;
    return AtaWriteSectors(lba, 1, buffer);
}

static u32 FatGetFatEntry(u32 cluster) {
    if (!g_sector_buf) return 0xFFFFFFFF;

    if (g_fat.type == FAT_TYPE_32) {
        u32 fat_offset = g_fat.fat_start_sector + (cluster * 4) / g_fat.bytes_per_sector;
        u32 entry_offset = (cluster * 4) % g_fat.bytes_per_sector;

        ntstatus status = FatReadSector(fat_offset, g_sector_buf);
        if (NT_ERROR(status)) return 0xFFFFFFFF;

        return *(u32*)(g_sector_buf + entry_offset) & 0x0FFFFFFF;
    }

    if (g_fat.type == FAT_TYPE_16) {
        u32 fat_offset = g_fat.fat_start_sector + (cluster * 2) / g_fat.bytes_per_sector;
        u32 entry_offset = (cluster * 2) % g_fat.bytes_per_sector;

        ntstatus status = FatReadSector(fat_offset, g_sector_buf);
        if (NT_ERROR(status)) return 0xFFFFFFFF;

        return (u32) * (u16*)(g_sector_buf + entry_offset);
    }

    /* FAT12: two entries packed into 3 bytes */
    u32 fat_offset = g_fat.fat_start_sector + (cluster * 3 / 2) / g_fat.bytes_per_sector;
    u32 entry_offset = (cluster * 3 / 2) % g_fat.bytes_per_sector;

    ntstatus status = FatReadSector(fat_offset, g_sector_buf);
    if (NT_ERROR(status)) return 0xFFFFFFFF;

    u16 val;
    if (entry_offset == g_fat.bytes_per_sector - 1) {
        u8 b0 = g_sector_buf[entry_offset];
        u8 b1_buf[2];
        ntstatus s2 = FatReadSector(fat_offset + 1, b1_buf);
        if (NT_ERROR(s2)) return 0xFFFFFFFF;
        val = (u16)b0 | ((u16)b1_buf[0] << 8);
    }
    else {
        val = *(u16*)(g_sector_buf + entry_offset);
    }

    if (cluster & 1) {
        return (u32)(val >> 4);
    }
    else {
        return (u32)(val & 0x0FFF);
    }
}

static u32 FatNextCluster(u32 cluster) {
    u32 next = FatGetFatEntry(cluster);
    u32 eof_mask;
    switch (g_fat.type) {
    case FAT_TYPE_12: eof_mask = 0x0FF8; break;
    case FAT_TYPE_16: eof_mask = 0xFFF8; break;
    default:          eof_mask = 0x0FFFFFF8; break;
    }
    if (next >= eof_mask) return 0;
    return next;
}

static u32 FatClusterToSector(u32 cluster) {
    return g_fat.data_start_sector + (cluster - 2) * g_fat.sectors_per_cluster;
}

static i32 FatIsMbr(u8* sector) {
    for (i32 i = 0; i < 4; i++) {
        u8* entry = sector + 0x1BE + i * 16;
        u8 boot_flag = entry[0];
        u8 type = entry[4];
        if ((boot_flag == 0x00 || boot_flag == 0x80) && type != 0x00) {
            return 1;
        }
    }
    return 0;
}

void Fat32Init(u64 partition_start_lba) {
    g_fat_initialized = 0;
    g_fat.partition_start_lba = partition_start_lba;

    g_sector_buf = (u8*)KmAlloc(ATA_SECTOR_SIZE);
    if (!g_sector_buf) {
        KdPanic("[FAT] Failed to allocate sector buffer");
    }

    ntstatus status = AtaReadSectors(0, 1, g_sector_buf);
    if (NT_ERROR(status)) {
        KdPrintf("[FAT] Failed to read boot sector\n");
        return;
    }

    if (g_sector_buf[510] != 0x55 || g_sector_buf[511] != 0xAA) {
        KdPrintf("[FAT] Invalid boot signature 0x%02x%02x\n",
            g_sector_buf[511], g_sector_buf[510]);
        return;
    }

    i32 is_gpt = 0;

    if (FatIsMbr(g_sector_buf)) {
        KdPrintf("[FAT] MBR detected, scanning partition table...\n");
        for (i32 i = 0; i < 4; i++) {
            u8* entry = g_sector_buf + 0x1BE + i * 16;
            u8 type = entry[4];
            u32 start_lba = *(u32*)(entry + 8);
            u32 sector_count = *(u32*)(entry + 12);

            KdPrintf("[FAT]   Partition %d: type=0x%02x start=%u count=%u\n",
                i, type, start_lba, sector_count);

            if (type == 0xEE) {
                is_gpt = 1;
                break;
            }

            if (type != 0 && start_lba != 0) {
                g_fat.partition_start_lba = start_lba;
                KdPrintf("[FAT] Using MBR partition %d at LBA %u\n", i, start_lba);
                break;
            }
        }
    }

    // ��������� ������� �������� GPT
    if (is_gpt) {
        KdPrintf("[FAT] Protective MBR detected. Switching to GPT parser...\n");

        u8* gpt_hdr_buf = (u8*)KmAlloc(512);
        if (!gpt_hdr_buf) {
            KdPanic("[FAT] Failed to allocate GPT header buffer");
        }

        status = AtaReadSectors(1, 1, gpt_hdr_buf);
        if (NT_ERROR(status)) {
            KdPrintf("[FAT] Failed to read GPT header at LBA 1\n");
            KmFree(gpt_hdr_buf);
            return;
        }

        GptHeader* header = (GptHeader*)gpt_hdr_buf;
        if (header->signature != 0x5452415020494645ULL) {
            KdPrintf("[FAT] Invalid GPT header signature!\n");
            KmFree(gpt_hdr_buf);
            return;
        }

        u64 entry_lba = header->partition_entry_lba;
        u32 num_entries = header->num_partition_entries;
        u32 entry_size = header->size_of_partition_entry;
        KmFree(gpt_hdr_buf);

        // GUID ��� Basic Data Partition (EBD0A0A2-B9E5-4433-87C0-68B6B72699C7)
        u8 basic_data_guid[16] = {
            0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
            0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
        };

        u8* entry_buf = (u8*)KmAlloc(512);
        if (!entry_buf) {
            KdPanic("[FAT] Failed to allocate GPT entry buffer");
        }

        i32 gpt_partition_found = 0;
        u32 entries_per_sector = 512 / entry_size;

        u8* vbr_check_buf = (u8*)KmAlloc(512);
        if (!vbr_check_buf) {
            KdPanic("[FAT] Failed to allocate VBR verification buffer");
        }

        // ���������� ������ �������� GPT
        for (u32 e = 0; e < num_entries; e++) {
            u32 sector_offset = e / entries_per_sector;
            u32 byte_offset = (e % entries_per_sector) * entry_size;

            if (e % entries_per_sector == 0) {
                status = AtaReadSectors(entry_lba + sector_offset, 1, entry_buf);
                if (NT_ERROR(status)) {
                    KdPrintf("[FAT] Failed to read GPT entry sector %llu\n", entry_lba + sector_offset);
                    break;
                }
            }

            GptEntry* entry = (GptEntry*)(entry_buf + byte_offset);

            // ������� GUID ���� �������
            i32 match = 1;
            for (i32 g = 0; g < 16; g++) {
                if (entry->type_guid[g] != basic_data_guid[g]) {
                    match = 0;
                    break;
                }
            }

            if (match && entry->starting_lba != 0) {
                // ������ ������ ������ ����� ������� ��� �������� �� ���������� ��������� FAT
                status = AtaReadSectors(entry->starting_lba, 1, vbr_check_buf);
                if (NT_SUCCESS(status)) {
                    u16 check_sig = *(u16*)(vbr_check_buf + 510);
                    u16 check_bps = *(u16*)(vbr_check_buf + 11);
                    u8  check_fats = vbr_check_buf[16];
                    u16 check_spf16 = *(u16*)(vbr_check_buf + 22);
                    u32 check_spf32 = *(u32*)(vbr_check_buf + 36);
                    u32 check_spf = check_spf16 ? check_spf16 : check_spf32;

                    // �������� FAT ������ ����� ��������� 0xAA55, ���������� ������ �������,
                    // � ����� ���������� ������ FAT > 0 � ��������� ������ FAT-�������.
                    if (check_sig == 0xAA55 && check_fats > 0 && check_spf > 0 &&
                        (check_bps == 512 || check_bps == 2048 || check_bps == 4096)) {

                        g_fat.partition_start_lba = entry->starting_lba;
                        KdPrintf("[FAT] Found valid GPT FAT Partition at LBA %llu\n", entry->starting_lba);
                        gpt_partition_found = 1;
                        break;
                    }
                    else {
                        // ���� ��� NTFS ��� exFAT, �������� ������� � ���� ������
                        KdPrintf("[FAT] Skipping Basic Data Partition at LBA %llu (Not FAT: FATS=%u, SPF=%u)\n",
                            entry->starting_lba, check_fats, check_spf);
                    }
                }
            }
        }

        KmFree(vbr_check_buf);
        KmFree(entry_buf);

        if (!gpt_partition_found) {
            KdPrintf("[FAT] No valid GPT FAT Partition found!\n");
            return;
        }
    }

    // ������ �������������� VBR
    status = FatReadSector(0, g_sector_buf);
    if (NT_ERROR(status)) {
        KdPrintf("[FAT] Failed to read partition boot sector\n");
        return;
    }
    if (g_sector_buf[510] != 0x55 || g_sector_buf[511] != 0xAA) {
        KdPrintf("[FAT] Partition boot sector invalid\n");
        return;
    }

    u16 bytes_per_sector = *(u16*)(g_sector_buf + 11);
    u8  sectors_per_cluster = g_sector_buf[13];
    u16 reserved_sectors = *(u16*)(g_sector_buf + 14);
    u8  num_fats = g_sector_buf[16];
    u16 root_entry_count = *(u16*)(g_sector_buf + 17);
    u16 total_sectors_16 = *(u16*)(g_sector_buf + 19);
    u32 total_sectors_32 = *(u32*)(g_sector_buf + 32);
    u32 sectors_per_fat_32 = *(u32*)(g_sector_buf + 36);
    u16 sectors_per_fat_16 = *(u16*)(g_sector_buf + 22);

    if (bytes_per_sector != 512) {
        KdPrintf("[FAT] Unsupported bytes_per_sector=%u\n", bytes_per_sector);
        return;
    }

    if (sectors_per_cluster == 0 || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0) {
        KdPrintf("[FAT] Invalid sectors_per_cluster=%u\n", sectors_per_cluster);
        return;
    }

    u32 total_sectors = total_sectors_16 ? total_sectors_16 : total_sectors_32;
    u32 sectors_per_fat = sectors_per_fat_16 ? sectors_per_fat_16 : sectors_per_fat_32;

    u32 root_dir_sectors = ((root_entry_count * 32) + (bytes_per_sector - 1)) / bytes_per_sector;
    u32 data_start = reserved_sectors + num_fats * sectors_per_fat + root_dir_sectors;
    u32 data_sectors = total_sectors - data_start;
    u32 total_clusters = data_sectors / sectors_per_cluster;

    FatType type;
    if (total_clusters < 4085) {
        type = FAT_TYPE_12;
    }
    else if (total_clusters < 65525) {
        type = FAT_TYPE_16;
    }
    else {
        type = FAT_TYPE_32;
    }

    g_fat.type = type;
    g_fat.bytes_per_sector = bytes_per_sector;
    g_fat.sectors_per_cluster = sectors_per_cluster;
    g_fat.reserved_sectors = reserved_sectors;
    g_fat.num_fats = num_fats;
    g_fat.root_entry_count = root_entry_count;
    g_fat.sectors_per_fat = sectors_per_fat;
    g_fat.total_sectors = total_sectors;
    g_fat.fat_start_sector = reserved_sectors;
    g_fat.root_dir_sectors = root_dir_sectors;
    g_fat.root_dir_start_sector = reserved_sectors + num_fats * sectors_per_fat;

    if (type == FAT_TYPE_32) {
        g_fat.root_cluster = *(u32*)(g_sector_buf + 44);
        g_fat.data_start_sector = reserved_sectors + num_fats * sectors_per_fat;
    }
    else {
        g_fat.root_cluster = 0;
        g_fat.data_start_sector = data_start;
    }

    g_fat_initialized = 1;

    const char* type_str = type == FAT_TYPE_12 ? "FAT12" :
        type == FAT_TYPE_16 ? "FAT16" : "FAT32";

    KdPrintf("[FAT] %s detected: BPS=%u SPC=%u reserved=%u FATS=%u SPF=%u\n",
        type_str, g_fat.bytes_per_sector, g_fat.sectors_per_cluster,
        g_fat.reserved_sectors, g_fat.num_fats, g_fat.sectors_per_fat);
    KdPrintf("[FAT] root_entries=%u root_dir_sectors=%u clusters=%u at LBA 0x%llx\n",
        g_fat.root_entry_count, g_fat.root_dir_sectors, total_clusters,
        g_fat.partition_start_lba);
}

ntstatus Fat32OpenRoot(void) {
    if (!g_fat_initialized) return STATUS_UNSUCCESSFUL;
    if (g_fat.type == FAT_TYPE_32) {
        g_current_cluster = g_fat.root_cluster;
    }
    else {
        g_current_cluster = 0;
    }
    g_current_file_size = 0;
    g_current_is_directory = 1;
    return STATUS_SUCCESS;
}

static ntstatus FatParseDirEntry(u8* raw, FatDirEntry* out) {
    u8 attr = raw[11];

    if (raw[0] == 0x00) return STATUS_NOT_FOUND;
    if (raw[0] == 0xE5) return STATUS_NOT_FOUND;
    if ((attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
        out->is_long_name = 1;
        return STATUS_SUCCESS;
    }

    RtMemSet(out, 0, sizeof(FatDirEntry));

    if (attr & FAT_ATTR_READONLY)  out->is_readonly = 1;
    if (attr & FAT_ATTR_HIDDEN)    out->is_hidden = 1;
    if (attr & FAT_ATTR_SYSTEM)    out->is_system = 1;
    if (attr & FAT_ATTR_VOLUME_ID) out->is_volume_label = 1;
    if (attr & FAT_ATTR_DIRECTORY) out->is_directory = 1;
    out->attributes = attr;

    u32 cluster_hi = g_fat.type == FAT_TYPE_32 ? *(u16*)(raw + 20) : 0;
    u32 cluster_lo = *(u16*)(raw + 26);
    out->first_cluster = (cluster_hi << 16) | cluster_lo;
    out->file_size = *(u32*)(raw + 28);

    char* p = out->name;
    for (i32 i = 0; i < 8; i++) {
        if (raw[i] == ' ') break;
        *p++ = (char)(raw[i] >= 'A' && raw[i] <= 'Z' ? raw[i] + 32 : raw[i]);
    }
    if (raw[8] != ' ') {
        *p++ = '.';
        for (i32 i = 8; i < 11; i++) {
            if (raw[i] == ' ') break;
            *p++ = (char)(raw[i] >= 'A' && raw[i] <= 'Z' ? raw[i] + 32 : raw[i]);
        }
    }
    *p = 0;

    return STATUS_SUCCESS;
}

ntstatus Fat32OpenPath(const char* path) {
    if (!g_fat_initialized) return STATUS_UNSUCCESSFUL;

    while (*path == '/' || *path == '\\') path++;
    if (*path == 0) {
        g_current_is_directory = 1;
        return Fat32OpenRoot();
    }

    ntstatus status = Fat32OpenRoot();
    if (NT_ERROR(status)) return status;

    char component[FAT_MAX_PATH];
    const char* p = path;

    while (*p) {
        i32 i = 0;
        while (*p && *p != '/' && *p != '\\' && i < FAT_MAX_PATH - 1) {
            component[i++] = *p++;
        }
        component[i] = 0;
        if (*p == '/' || *p == '\\') p++;

        if (i == 0) continue;

        FatDirEntry* entries = (FatDirEntry*)KmAlloc(sizeof(FatDirEntry) * 64);
        if (!entries) return STATUS_OUT_OF_MEMORY;
        u32 count = 64;
        status = Fat32ReadDir(entries, &count);
        if (NT_ERROR(status)) { KmFree(entries); return status; }

        i32 found = 0;
        for (u32 j = 0; j < count; j++) {
            if (entries[j].is_long_name) continue;
            if (RtStrCompareI(entries[j].name, component) == 0) {
                g_current_cluster = entries[j].first_cluster;
                g_current_file_size = entries[j].file_size;
                g_current_is_directory = entries[j].is_directory;
                found = 1;
                if (!entries[j].is_directory) { KmFree(entries); return STATUS_SUCCESS; }
                break;
            }
            char short_cmp[12];
            FatGenerateShortName(component, short_cmp);
            char short_entry[12];
            FatGenerateShortName(entries[j].name, short_entry);
            if (RtMemCompare(short_entry, short_cmp, 11) == 0) {
                g_current_cluster = entries[j].first_cluster;
                g_current_file_size = entries[j].file_size;
                g_current_is_directory = entries[j].is_directory;
                found = 1;
                if (!entries[j].is_directory) { KmFree(entries); return STATUS_SUCCESS; }
                break;
            }
        }
        KmFree(entries);

        if (!found) return STATUS_NO_SUCH_FILE;
    }

    g_current_is_directory = 1;
    return STATUS_SUCCESS;
}

ntstatus Fat32OpenDir(const char* name) {
    if (!g_fat_initialized) return STATUS_UNSUCCESSFUL;
    if (!name || !*name) return Fat32OpenRoot();

    ntstatus status = Fat32OpenRoot();
    if (NT_ERROR(status)) return status;

    FatDirEntry* entries = (FatDirEntry*)KmAlloc(sizeof(FatDirEntry) * 128);
    if (!entries) return STATUS_OUT_OF_MEMORY;
    u32 count = 128;
    status = Fat32ReadDir(entries, &count);
    if (NT_ERROR(status)) { KmFree(entries); return status; }

    i32 found = 0;
    for (u32 j = 0; j < count; j++) {
        if (entries[j].is_long_name) continue;
        if (RtStrCompareI(entries[j].name, name) == 0 && entries[j].is_directory) {
            g_current_cluster = entries[j].first_cluster;
            g_current_file_size = 0;
            g_current_is_directory = 1;
            found = 1;
            break;
        }
    }
    KmFree(entries);

    if (!found) return STATUS_NOT_FOUND;
    return STATUS_SUCCESS;
}

u8 Fat32IsCurrentDirectory(void) {
    return g_current_is_directory;
}

static void FatExtractLfn(u8* raw, char* lfn_buf, u32 lfn_buf_size) {
    u8 seq = raw[0] & 0x3F;
    u32 base = (u32)(seq - 1) * 13;
    u16 chars[13];

    chars[0] = *(u16*)(raw + 1);
    chars[1] = *(u16*)(raw + 3);
    chars[2] = *(u16*)(raw + 5);
    chars[3] = *(u16*)(raw + 7);
    chars[4] = *(u16*)(raw + 9);
    chars[5] = *(u16*)(raw + 14);
    chars[6] = *(u16*)(raw + 16);
    chars[7] = *(u16*)(raw + 18);
    chars[8] = *(u16*)(raw + 20);
    chars[9] = *(u16*)(raw + 22);
    chars[10] = *(u16*)(raw + 24);
    chars[11] = *(u16*)(raw + 28);
    chars[12] = *(u16*)(raw + 30);

    for (i32 i = 0; i < 13; i++) {
        u32 pos = base + (u32)i;
        if (pos >= lfn_buf_size - 1) break;
        if (chars[i] == 0x0000 || chars[i] == 0xFFFF) {
            lfn_buf[pos] = 0;
            return;
        }
        lfn_buf[pos] = (char)(chars[i] & 0xFF);
    }
}

ntstatus Fat32ReadDir(FatDirEntry* entries, u32* count) {
    if (!g_fat_initialized || !entries || !count) return STATUS_INVALID_PARAMETER;

    u32 max = *count;
    u32 idx = 0;
    char lfn_accum[260];
    i32 lfn_valid = 0;
    RtMemSet(lfn_accum, 0, sizeof(lfn_accum));

    u32 cluster = g_current_cluster;
    u32 root_sec = 0;
    u32 root_total = 0;

    if (g_fat.type != FAT_TYPE_32 && cluster == 0) {
        root_sec = g_fat.root_dir_start_sector;
        root_total = g_fat.root_dir_sectors;
    }

    i32 done = 0;
    while (!done && idx < max) {
        u32 sectors_in_group;
        u32 sector_base;

        if (g_fat.type != FAT_TYPE_32 && g_current_cluster == 0) {
            if (root_total == 0) break;
            sectors_in_group = root_total;
            sector_base = root_sec;
            done = 1;
        }
        else {
            if (cluster == 0 || cluster >= 0x0FFFFFF8) break;
            sectors_in_group = g_fat.sectors_per_cluster;
            sector_base = FatClusterToSector(cluster);
            cluster = FatNextCluster(cluster);
        }

        for (u32 s = 0; s < sectors_in_group && idx < max; s++) {
            ntstatus status = FatReadSector(sector_base + s, g_sector_buf);
            if (NT_ERROR(status)) return status;

            for (u32 d = 0; d < g_fat.bytes_per_sector && idx < max; d += 32) {
                if (g_sector_buf[d] == 0x00) {
                    *count = idx;
                    return STATUS_SUCCESS;
                }
                if (g_sector_buf[d] == 0xE5) {
                    lfn_valid = 0;
                    RtMemSet(lfn_accum, 0, sizeof(lfn_accum));
                    continue;
                }

                u8 attr = g_sector_buf[d + 11];
                if ((attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
                    FatExtractLfn(g_sector_buf + d, lfn_accum, sizeof(lfn_accum));
                    lfn_valid = 1;
                    continue;
                }

                if (attr & FAT_ATTR_VOLUME_ID) {
                    lfn_valid = 0;
                    continue;
                }

                FatDirEntry entry;
                ntstatus s2 = FatParseDirEntry(g_sector_buf + d, &entry);
                if (s2 == STATUS_NOT_FOUND) {
                    lfn_valid = 0;
                    continue;
                }

                if (lfn_valid && lfn_accum[0]) {
                    i32 len = 0;
                    for (i32 k = 0; k < 260 && lfn_accum[k]; k++) len = k + 1;
                    if (len > 255) len = 255;
                    RtMemCopy(entry.name, lfn_accum, (u32)len);
                    entry.name[len] = 0;
                }

                lfn_valid = 0;
                RtMemSet(lfn_accum, 0, sizeof(lfn_accum));

                if (entry.is_long_name) continue;
                entries[idx++] = entry;
            }
        }
    }

    *count = idx;
    return STATUS_SUCCESS;
}

ntstatus Fat32ReadFile(void* buffer, u32* bytes_read) {
    if (!g_fat_initialized) return STATUS_UNSUCCESSFUL;

    u8* dst = (u8*)buffer;
    u32 remaining = g_current_file_size;
    u32 cluster = g_current_cluster;
    u32 total_read = 0;

    while (cluster && cluster < 0x0FFFFFF8 && remaining > 0) {
        u32 sector = FatClusterToSector(cluster);

        for (u32 s = 0; s < g_fat.sectors_per_cluster && remaining > 0; s++) {
            ntstatus status = FatReadSector(sector + s, g_sector_buf);
            if (NT_ERROR(status)) {
                if (bytes_read) *bytes_read = total_read;
                return status;
            }

            u32 to_copy = remaining;
            if (to_copy > g_fat.bytes_per_sector) to_copy = g_fat.bytes_per_sector;
            RtMemCopy(dst, g_sector_buf, to_copy);
            dst += to_copy;
            remaining -= to_copy;
            total_read += to_copy;
        }

        cluster = FatNextCluster(cluster);
    }

    if (bytes_read) *bytes_read = total_read;
    return STATUS_SUCCESS;
}

u32 Fat32GetFileSize(void) { return g_current_file_size; }

void Fat32Close(void) {
    g_current_cluster = 0;
    g_current_file_size = 0;
}

ntstatus Fat32ReadFileAt(const char* path, u32 offset, void* buffer, u32 count, u32* bytes_read) {
    if (!g_fat_initialized) return STATUS_UNSUCCESSFUL;
    if (bytes_read) *bytes_read = 0;

    ntstatus status = Fat32OpenPath(path);
    if (NT_ERROR(status)) return status;

    u32 file_size = g_current_file_size;
    if (offset >= file_size) { Fat32Close(); return STATUS_END_OF_FILE; }

    u32 to_read = count;
    if (offset + to_read > file_size) to_read = file_size - offset;

    u32 cluster_size = g_fat.sectors_per_cluster * g_fat.bytes_per_sector;
    u32 cluster = g_current_cluster;

    u32 pos = 0;
    while (cluster && cluster < 0x0FFFFFF8 && pos + cluster_size <= offset) {
        pos += cluster_size;
        cluster = FatNextCluster(cluster);
    }

    if (!cluster || cluster >= 0x0FFFFFF8) { Fat32Close(); return STATUS_END_OF_FILE; }

    u32 skip_in_cluster = offset - pos;
    u32 total_read = 0;
    u8* dst = (u8*)buffer;

    while (cluster && cluster < 0x0FFFFFF8 && to_read > 0) {
        u32 sector = FatClusterToSector(cluster);

        for (u32 s = 0; s < g_fat.sectors_per_cluster && to_read > 0; s++) {
            status = FatReadSector(sector + s, g_sector_buf);
            if (NT_ERROR(status)) { Fat32Close(); if (bytes_read) *bytes_read = total_read; return status; }

            if (skip_in_cluster >= g_fat.bytes_per_sector) {
                skip_in_cluster -= g_fat.bytes_per_sector;
                continue;
            }

            u32 start = skip_in_cluster;
            skip_in_cluster = 0;
            u32 avail = g_fat.bytes_per_sector - start;
            u32 to_copy = to_read;
            if (to_copy > avail) to_copy = avail;

            RtMemCopy(dst, g_sector_buf + start, to_copy);
            dst += to_copy;
            to_read -= to_copy;
            total_read += to_copy;
        }

        cluster = FatNextCluster(cluster);
    }

    if (bytes_read) *bytes_read = total_read;
    Fat32Close();
    return STATUS_SUCCESS;
}

static void FatGenerateShortName(const char* long_name, char* short_name) {
    RtMemSet(short_name, ' ', 11);

    const char* dot = NULL;
    usize name_len = RtStrLen(long_name);
    for (usize i = name_len; i > 0; i--) {
        if (long_name[i - 1] == '.') { dot = long_name + i - 1; break; }
    }

    usize base_len = dot ? (usize)(dot - long_name) : name_len;
    if (base_len > 8) base_len = 8;
    for (usize i = 0; i < base_len; i++) {
        char c = long_name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        else if (c == ' ') c = '_';
        short_name[i] = c;
    }

    if (dot) {
        dot++;
        usize ext_len = RtStrLen(dot);
        if (ext_len > 3) ext_len = 3;
        for (usize i = 0; i < ext_len; i++) {
            char c = dot[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            else if (c == ' ') c = '_';
            short_name[8 + i] = c;
        }
    }
}

static ntstatus FatSetFatEntry(u32 cluster, u32 value) {
    if (!g_fat_initialized || !g_sector_buf) return STATUS_UNSUCCESSFUL;
    if (cluster < 2) return STATUS_INVALID_PARAMETER;

    if (g_fat.type == FAT_TYPE_32) {
        u32 fat_sector_offset = (cluster * 4) / g_fat.bytes_per_sector;
        u32 entry_offset = (cluster * 4) % g_fat.bytes_per_sector;

        for (u32 fat_num = 0; fat_num < g_fat.num_fats; fat_num++) {
            u32 sec = g_fat.fat_start_sector + fat_num * g_fat.sectors_per_fat + fat_sector_offset;

            ntstatus status = FatReadSector(sec, g_sector_buf);
            if (NT_ERROR(status)) return status;

            *(u32*)(g_sector_buf + entry_offset) =
                (*(u32*)(g_sector_buf + entry_offset) & 0xF0000000) | (value & 0x0FFFFFFF);

            status = FatWriteSector(sec, g_sector_buf);
            if (NT_ERROR(status)) return status;
        }
        return STATUS_SUCCESS;
    }

    if (g_fat.type == FAT_TYPE_16) {
        u32 fat_sector_offset = (cluster * 2) / g_fat.bytes_per_sector;
        u32 entry_offset = (cluster * 2) % g_fat.bytes_per_sector;

        for (u32 fat_num = 0; fat_num < g_fat.num_fats; fat_num++) {
            u32 sec = g_fat.fat_start_sector + fat_num * g_fat.sectors_per_fat + fat_sector_offset;

            ntstatus status = FatReadSector(sec, g_sector_buf);
            if (NT_ERROR(status)) return status;

            *(u16*)(g_sector_buf + entry_offset) = (u16)value;

            status = FatWriteSector(sec, g_sector_buf);
            if (NT_ERROR(status)) return status;
        }
        return STATUS_SUCCESS;
    }

    u32 fat_offset_bytes = cluster * 3 / 2;
    u32 fat_sector_offset = fat_offset_bytes / g_fat.bytes_per_sector;
    u32 entry_offset = fat_offset_bytes % g_fat.bytes_per_sector;

    for (u32 fat_num = 0; fat_num < g_fat.num_fats; fat_num++) {
        u32 sec = g_fat.fat_start_sector + fat_num * g_fat.sectors_per_fat + fat_sector_offset;

        ntstatus status = FatReadSector(sec, g_sector_buf);
        if (NT_ERROR(status)) return status;

        if (entry_offset == g_fat.bytes_per_sector - 1) {
            u8 b0 = g_sector_buf[entry_offset];

            u8 next_buf[512];
            ntstatus s2 = FatReadSector(sec + 1, next_buf);
            if (NT_ERROR(s2)) return s2;
            u8 b1 = next_buf[0];

            if (cluster & 1) {
                b0 = (u8)((b0 & 0x0F) | ((value & 0x0F) << 4));
                b1 = (u8)(value >> 4);
            } else {
                b0 = (u8)(value & 0xFF);
                b1 = (u8)((b1 & 0xF0) | ((value >> 8) & 0x0F));
            }

            g_sector_buf[entry_offset] = b0;
            status = FatWriteSector(sec, g_sector_buf);
            if (NT_ERROR(status)) return status;

            next_buf[0] = b1;
            status = FatWriteSector(sec + 1, next_buf);
            if (NT_ERROR(status)) return status;
        } else {
            u16 val = *(u16*)(g_sector_buf + entry_offset);
            if (cluster & 1) {
                val = (u16)((val & 0x000F) | ((u16)(value << 4)));
            } else {
                val = (u16)((val & 0xF000) | (value & 0x0FFF));
            }
            *(u16*)(g_sector_buf + entry_offset) = val;
            status = FatWriteSector(sec, g_sector_buf);
            if (NT_ERROR(status)) return status;
        }
    }

    return STATUS_SUCCESS;
}

static u32 FatEofMarker(void) {
    switch (g_fat.type) {
    case FAT_TYPE_12: return 0xFF8;
    case FAT_TYPE_16: return 0xFFF8;
    default:          return 0x0FFFFFF8;
    }
}

static u32 FatAllocateCluster(void) {
    if (!g_fat_initialized) return 0;

    u32 eof = FatEofMarker();

    u32 data_sectors = g_fat.total_sectors - g_fat.data_start_sector;
    u32 max_cluster = data_sectors / g_fat.sectors_per_cluster + 2;

    for (u32 c = 2; c < max_cluster; c++) {
        u32 val = FatGetFatEntry(c);
        if (val == 0) {
            ntstatus status = FatSetFatEntry(c, eof);
            if (NT_ERROR(status)) return 0;
            KdPrintf("[FAT] Allocated cluster %u\n", c);
            return c;
        }
    }

    KdPrintf("[FAT] Disk full - no free clusters\n");
    return 0;
}

static void FatFreeClusterChain(u32 cluster) {
    if (!g_fat_initialized || cluster < 2) return;

    u32 eof = FatEofMarker();

    while (cluster >= 2 && cluster < eof) {
        u32 next = FatGetFatEntry(cluster);
        FatSetFatEntry(cluster, 0);
        if (next < 2 || next >= eof) break;
        cluster = next;
    }
}

static u8 FatComputeChecksum(const char* short_name) {
    u8 sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (u8)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + (u8)short_name[i]);
    }
    return sum;
}

static i32 FatNeedLfn(const char* name) {
    if (RtStrCompare(name, ".") == 0 || RtStrCompare(name, "..") == 0) return 0;
    
    usize len = RtStrLen(name);
    if (len > 12) return 1;
    
    for (usize i = 0; i < len; i++) {
        if (name[i] >= 'a' && name[i] <= 'z') return 1;
    }
    
    const char* dot = NULL;
    for (usize i = 0; i < len; i++) {
        if (name[i] == '.') {
            if (dot) return 1;
            dot = name + i;
        }
    }
    
    if (dot) {
        usize base_len = (usize)(dot - name);
        usize ext_len = RtStrLen(dot + 1);
        if (base_len > 8 || ext_len > 3) return 1;
    } else {
        if (len > 8) return 1;
    }
    
    for (usize i = 0; i < len; i++) {
        char c = name[i];
        if (c == ' ' || c == '+' || c == ',' || c == ';' || c == '=' || c == '[' || c == ']') {
            return 1;
        }
    }
    
    return 0;
}

static void FatFormatLfnEntry(u8* entry, const char* long_name, u8 seq, u8 checksum, u32 name_offset, u32 name_len) {
    RtMemSet(entry, 0, 32);
    entry[0] = seq;
    entry[11] = FAT_ATTR_LFN;
    entry[12] = 0;
    entry[13] = checksum;
    *(u16*)(entry + 26) = 0;
    
    u16 chars[13];
    for (int i = 0; i < 13; i++) {
        u32 char_idx = name_offset + i;
        if (char_idx < name_len) {
            chars[i] = (u16)(u8)long_name[char_idx];
        } else if (char_idx == name_len) {
            chars[i] = 0x0000;
        } else {
            chars[i] = 0xFFFF;
        }
    }

    *(u16*)(entry + 1) = chars[0];
    *(u16*)(entry + 3) = chars[1];
    *(u16*)(entry + 5) = chars[2];
    *(u16*)(entry + 7) = chars[3];
    *(u16*)(entry + 9) = chars[4];

    *(u16*)(entry + 14) = chars[5];
    *(u16*)(entry + 16) = chars[6];
    *(u16*)(entry + 18) = chars[7];
    *(u16*)(entry + 20) = chars[8];
    *(u16*)(entry + 22) = chars[9];
    *(u16*)(entry + 24) = chars[10];

    *(u16*)(entry + 28) = chars[11];
    *(u16*)(entry + 30) = chars[12];
}

static ntstatus FatFindEntry(u32 dir_cluster, const char* name,
                             u32* out_entry_sector, u32* out_entry_offset,
                             u32* out_first_cluster, u32* out_file_size,
                             u32* out_lfn_start_sector, u32* out_lfn_start_offset) {
    u32 scan_cluster = dir_cluster;
    u32 root_sec = 0;
    u32 root_total = 0;

    if (g_fat.type != FAT_TYPE_32 && dir_cluster == 0) {
        root_sec = g_fat.root_dir_start_sector;
        root_total = g_fat.root_dir_sectors;
    }

    char lfn_accum[260];
    i32 lfn_valid = 0;
    u32 lfn_start_sec = 0;
    u32 lfn_start_off = 0;
    RtMemSet(lfn_accum, 0, sizeof(lfn_accum));

    i32 scan_done = 0;
    while (!scan_done) {
        u32 sector_base;
        u32 sectors_in_group;

        if (g_fat.type != FAT_TYPE_32 && dir_cluster == 0) {
            if (root_total == 0) break;
            sectors_in_group = root_total;
            sector_base = root_sec;
            scan_done = 1;
        } else {
            if (scan_cluster < 2 || scan_cluster >= FatEofMarker()) break;
            sectors_in_group = g_fat.sectors_per_cluster;
            sector_base = FatClusterToSector(scan_cluster);
            scan_cluster = FatNextCluster(scan_cluster);
        }

        for (u32 s = 0; s < sectors_in_group; s++) {
            u32 current_sector = sector_base + s;
            ntstatus status = FatReadSector(current_sector, g_sector_buf);
            if (NT_ERROR(status)) return status;

            for (u32 d = 0; d < g_fat.bytes_per_sector; d += 32) {
                u8 first_byte = g_sector_buf[d];

                if (first_byte == 0x00) {
                    return STATUS_NO_SUCH_FILE;
                }
                if (first_byte == 0xE5) {
                    lfn_valid = 0;
                    RtMemSet(lfn_accum, 0, sizeof(lfn_accum));
                    continue;
                }

                u8 attr = g_sector_buf[d + 11];

                if ((attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
                    u8 seq = g_sector_buf[d];
                    if (seq & 0x40) {
                        lfn_start_sec = current_sector;
                        lfn_start_off = d;
                        lfn_valid = 1;
                        RtMemSet(lfn_accum, 0, sizeof(lfn_accum));
                    }
                    if (lfn_valid) {
                        FatExtractLfn(g_sector_buf + d, lfn_accum, sizeof(lfn_accum));
                    }
                    continue;
                }

                if (attr & FAT_ATTR_VOLUME_ID) {
                    lfn_valid = 0;
                    RtMemSet(lfn_accum, 0, sizeof(lfn_accum));
                    continue;
                }

                FatDirEntry entry;
                ntstatus s2 = FatParseDirEntry(g_sector_buf + d, &entry);
                if (s2 == STATUS_NOT_FOUND) {
                    lfn_valid = 0;
                    RtMemSet(lfn_accum, 0, sizeof(lfn_accum));
                    continue;
                }

                i32 name_match = 0;
                
                if (lfn_valid && lfn_accum[0]) {
                    if (RtStrCompareI(lfn_accum, name) == 0) {
                        name_match = 1;
                    }
                }
                
                if (!name_match) {
                    if (RtStrCompareI(entry.name, name) == 0) {
                        name_match = 1;
                    }
                }

                if (name_match) {
                    if (out_entry_sector) *out_entry_sector = current_sector;
                    if (out_entry_offset) *out_entry_offset = d;
                    if (out_first_cluster) *out_first_cluster = entry.first_cluster;
                    if (out_file_size) *out_file_size = entry.file_size;
                    if (lfn_valid) {
                        if (out_lfn_start_sector) *out_lfn_start_sector = lfn_start_sec;
                        if (out_lfn_start_offset) *out_lfn_start_offset = lfn_start_off;
                    } else {
                        if (out_lfn_start_sector) *out_lfn_start_sector = 0;
                        if (out_lfn_start_offset) *out_lfn_start_offset = 0;
                    }
                    return STATUS_SUCCESS;
                }

                lfn_valid = 0;
                RtMemSet(lfn_accum, 0, sizeof(lfn_accum));
            }
        }
    }

    return STATUS_NO_SUCH_FILE;
}

static ntstatus FatFindFreeSlots(u32 dir_cluster, u32 slots_needed, u32* out_sector, u32* out_offset) {
    u32 scan_cluster = dir_cluster;
    u32 root_sec = 0;
    u32 root_total = 0;

    if (g_fat.type != FAT_TYPE_32 && dir_cluster == 0) {
        root_sec = g_fat.root_dir_start_sector;
        root_total = g_fat.root_dir_sectors;
    }

    u32 run_start_sector = 0;
    u32 run_start_offset = 0;
    u32 run_length = 0;

    i32 scan_done = 0;
    while (!scan_done) {
        u32 sector_base;
        u32 sectors_in_group;

        if (g_fat.type != FAT_TYPE_32 && dir_cluster == 0) {
            if (root_total == 0) break;
            sectors_in_group = root_total;
            sector_base = root_sec;
            scan_done = 1;
        } else {
            if (scan_cluster < 2 || scan_cluster >= FatEofMarker()) break;
            sectors_in_group = g_fat.sectors_per_cluster;
            sector_base = FatClusterToSector(scan_cluster);
            scan_cluster = FatNextCluster(scan_cluster);
        }

        run_length = 0;

        for (u32 s = 0; s < sectors_in_group; s++) {
            u32 current_sector = sector_base + s;
            ntstatus status = FatReadSector(current_sector, g_sector_buf);
            if (NT_ERROR(status)) return status;

            for (u32 d = 0; d < g_fat.bytes_per_sector; d += 32) {
                u8 first_byte = g_sector_buf[d];

                if (first_byte == 0x00) {
                    if (run_length == 0) {
                        run_start_sector = current_sector;
                        run_start_offset = d;
                    }
                    u32 slots_remaining_in_group = ((sectors_in_group - s) * g_fat.bytes_per_sector - d) / 32;
                    if (slots_remaining_in_group >= slots_needed) {
                        *out_sector = run_start_sector;
                        *out_offset = run_start_offset;
                        return STATUS_SUCCESS;
                    } else {
                        run_length = 0;
                        goto next_cluster;
                    }
                }

                if (first_byte == 0xE5) {
                    if (run_length == 0) {
                        run_start_sector = current_sector;
                        run_start_offset = d;
                    }
                    run_length++;
                    if (run_length == slots_needed) {
                        *out_sector = run_start_sector;
                        *out_offset = run_start_offset;
                        return STATUS_SUCCESS;
                    }
                } else {
                    run_length = 0;
                }
            }
        }
    next_cluster:;
    }

    return STATUS_NOT_FOUND;
}

static ntstatus FatExpandDirectory(u32 dir_cluster, u32* out_sector) {
    if (g_fat.type != FAT_TYPE_32 && dir_cluster == 0) {
        return STATUS_DISK_FULL;
    }

    u32 last = dir_cluster;
    u32 next;
    while (last >= 2 && last < FatEofMarker() &&
           (next = FatNextCluster(last)) >= 2 && next < FatEofMarker()) {
        last = next;
    }

    u32 new_cluster = FatAllocateCluster();
    if (new_cluster == 0) return STATUS_DISK_FULL;

    if (last >= 2 && last < FatEofMarker()) {
        FatSetFatEntry(last, new_cluster);
    }

    u32 sector = FatClusterToSector(new_cluster);
    RtMemSet(g_sector_buf, 0, g_fat.bytes_per_sector);
    for (u32 s = 0; s < g_fat.sectors_per_cluster; s++) {
        FatWriteSector(sector + s, g_sector_buf);
    }

    *out_sector = sector;
    return STATUS_SUCCESS;
}

static ntstatus FatWriteDirectoryEntries(u32 start_sector, u32 start_offset, 
                                         const char* long_name, const char* short_name, 
                                         u8 attr, u32 first_cluster, u32 size) {
    u32 slots_needed = 1;
    i32 need_lfn = long_name ? FatNeedLfn(long_name) : 0;
    u32 lfn_entries = 0;
    u8 checksum = 0;

    if (need_lfn) {
        lfn_entries = (u32)((RtStrLen(long_name) + 12) / 13);
        slots_needed = lfn_entries + 1;
        checksum = FatComputeChecksum(short_name);
    }

    u32 cur_sector = start_sector;
    u32 cur_offset = start_offset;
    u8 entry_buf[32];

    for (u32 i = 0; i < slots_needed; i++) {
        if (need_lfn && i < lfn_entries) {
            u32 lfn_idx = lfn_entries - 1 - i;
            u8 seq = (u8)(lfn_idx + 1);
            if (i == 0) {
                seq |= 0x40;
            }
            FatFormatLfnEntry(entry_buf, long_name, seq, checksum, lfn_idx * 13, (u32)RtStrLen(long_name));
        } else {
            RtMemSet(entry_buf, 0, 32);
            RtMemCopy(entry_buf, short_name, 11);
            entry_buf[11] = attr;
            entry_buf[12] = 0;
            entry_buf[13] = 0;
            RtMemSet(entry_buf + 14, 0, 6);
            
            if (g_fat.type == FAT_TYPE_32) {
                *(u16*)(entry_buf + 20) = (u16)(first_cluster >> 16);
                *(u16*)(entry_buf + 26) = (u16)(first_cluster & 0xFFFF);
            } else {
                entry_buf[20] = 0; entry_buf[21] = 0;
                *(u16*)(entry_buf + 26) = (u16)(first_cluster & 0xFFFF);
            }
            *(u32*)(entry_buf + 28) = size;
        }

        ntstatus status = FatReadSector(cur_sector, g_sector_buf);
        if (NT_ERROR(status)) return status;

        RtMemCopy(g_sector_buf + cur_offset, entry_buf, 32);

        if (i == slots_needed - 1) {
            if (cur_offset + 32 < g_fat.bytes_per_sector) {
                if (g_sector_buf[cur_offset + 32] == 0x00) {
                    g_sector_buf[cur_offset + 32] = 0x00;
                }
            }
        }

        status = FatWriteSector(cur_sector, g_sector_buf);
        if (NT_ERROR(status)) return status;

        cur_offset += 32;
        if (cur_offset >= g_fat.bytes_per_sector) {
            cur_offset = 0;
            cur_sector++;
        }
    }

    return STATUS_SUCCESS;
}

ntstatus Fat32WriteFile(const char* path, const void* data, u32 size) {
    if (!g_fat_initialized || !g_sector_buf) return STATUS_UNSUCCESSFUL;
    if (!path || !*path) return STATUS_INVALID_PARAMETER;

    const char* p = path;
    while (*p == '/') p++;
    if (*p == 0) return STATUS_INVALID_PARAMETER;

    const char* last_slash = NULL;
    const char* scan = p;
    while (*scan) { if (*scan == '/') last_slash = scan; scan++; }

    char parent_path[FAT_MAX_PATH];
    char filename[256];

    if (last_slash) {
        u32 parent_len = (u32)(last_slash - p);
        RtMemCopy(parent_path, p, parent_len);
        parent_path[parent_len] = 0;
        RtStrCopy(filename, last_slash + 1);
    } else {
        parent_path[0] = 0;
        RtStrCopy(filename, p);
    }

    if (filename[0] == 0) return STATUS_INVALID_PARAMETER;

    char short_name[11];
    FatGenerateShortName(filename, short_name);

    KdPrintf("[FAT] WriteFile: path='%s' parent='%s' file='%s' size=%u\n",
             path, parent_path, filename, size);

    u32 dir_cluster;
    if (parent_path[0] == 0) {
        dir_cluster = (g_fat.type == FAT_TYPE_32) ? g_fat.root_cluster : 0;
    } else {
        ntstatus status = Fat32OpenPath(parent_path);
        if (NT_ERROR(status)) {
            KdPrintf("[FAT] Parent directory not found: %s\n", parent_path);
            return STATUS_NOT_FOUND;
        }
        dir_cluster = g_current_cluster;
        Fat32Close();
    }

    // -----------------------------------------------------------------------
    // STEP 1: Scan the directory to find an existing entry (or a free slot).
    // We must do this BEFORE allocating clusters so that FatAllocateCluster()
    // does not hand us the same clusters the existing file is still using.
    // -----------------------------------------------------------------------
    u32 entry_sector = 0;
    u32 entry_offset = 0;
    i32 found_existing = 0;
    u32 old_first_cluster = 0;

    u32 scan_cluster = dir_cluster;
    u32 root_sec = 0;
    u32 root_total = 0;

    if (g_fat.type != FAT_TYPE_32 && dir_cluster == 0) {
        root_sec = g_fat.root_dir_start_sector;
        root_total = g_fat.root_dir_sectors;
    }

    i32 scan_done = 0;
    while (!scan_done) {
        u32 sector_base;
        u32 sectors_in_group;

        if (g_fat.type != FAT_TYPE_32 && dir_cluster == 0) {
            if (root_total == 0) break;
            sectors_in_group = root_total;
            sector_base = root_sec;
            scan_done = 1;
        } else {
            if (scan_cluster < 2 || scan_cluster >= FatEofMarker()) break;
            sectors_in_group = g_fat.sectors_per_cluster;
            sector_base = FatClusterToSector(scan_cluster);
            scan_cluster = FatNextCluster(scan_cluster);
        }

        for (u32 s = 0; s < sectors_in_group; s++) {
            ntstatus status = FatReadSector(sector_base + s, g_sector_buf);
            if (NT_ERROR(status)) return status;

            for (u32 d = 0; d < g_fat.bytes_per_sector; d += 32) {
                if (g_sector_buf[d] == 0x00) {
                    scan_done = 1;
                    goto done_scan;
                }

                if (g_sector_buf[d] == 0xE5) {
                    continue;
                }

                u8 attr = g_sector_buf[d + 11];
                if ((attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue;
                if (attr & FAT_ATTR_VOLUME_ID) continue;

                i32 match = 1;
                for (i32 i = 0; i < 11; i++) {
                    if (g_sector_buf[d + i] != (u8)short_name[i]) { match = 0; break; }
                }

                if (match) {
                    u8 entry_attr = g_sector_buf[d + 11];
                    if (entry_attr & FAT_ATTR_DIRECTORY) {
                        return STATUS_ACCESS_DENIED;
                    }
                    entry_sector = sector_base + s;
                    entry_offset = d;
                    found_existing = 1;
                    old_first_cluster = (g_fat.type == FAT_TYPE_32) ?
                        ((u32)(*(u16*)(g_sector_buf + d + 20)) << 16) | *(u16*)(g_sector_buf + d + 26) :
                        *(u16*)(g_sector_buf + d + 26);
                    goto done_scan;
                }
            }
        }
    }

done_scan:

    // -----------------------------------------------------------------------
    // STEP 2: Free OLD clusters BEFORE allocating new ones.
    // This guarantees FatAllocateCluster() will never return a cluster that
    // still holds the old file's data (which we are about to overwrite).
    // -----------------------------------------------------------------------
    if (found_existing && old_first_cluster >= 2) {
        FatFreeClusterChain(old_first_cluster);
    }

    // -----------------------------------------------------------------------
    // STEP 3: Allocate new clusters and write data.
    // -----------------------------------------------------------------------
    u32 first_cluster = 0;
    u32 prev_cluster = 0;

    if (size > 0) {
        u32 bytes_per_cluster = g_fat.sectors_per_cluster * g_fat.bytes_per_sector;
        u32 clusters_needed = (size + bytes_per_cluster - 1) / bytes_per_cluster;

        for (u32 i = 0; i < clusters_needed; i++) {
            u32 c = FatAllocateCluster();
            if (c == 0) {
                if (first_cluster) FatFreeClusterChain(first_cluster);
                return STATUS_DISK_FULL;
            }
            if (prev_cluster) {
                FatSetFatEntry(prev_cluster, c);
            } else {
                first_cluster = c;
            }
            prev_cluster = c;
        }
    }

    if (size > 0 && first_cluster) {
        const u8* src = (const u8*)data;
        u32 remaining = size;
        u32 cluster = first_cluster;

        while (cluster >= 2 && cluster < FatEofMarker() && remaining > 0) {
            u32 sector = FatClusterToSector(cluster);

            for (u32 s = 0; s < g_fat.sectors_per_cluster && remaining > 0; s++) {
                ntstatus status = FatReadSector(sector + s, g_sector_buf);
                if (NT_ERROR(status)) {
                    FatFreeClusterChain(first_cluster);
                    return status;
                }

                u32 to_write = remaining;
                if (to_write > g_fat.bytes_per_sector) to_write = g_fat.bytes_per_sector;
                RtMemCopy(g_sector_buf, src, to_write);
                if (to_write < g_fat.bytes_per_sector)
                    RtMemSet(g_sector_buf + to_write, 0, g_fat.bytes_per_sector - to_write);

                status = FatWriteSector(sector + s, g_sector_buf);
                if (NT_ERROR(status)) {
                    FatFreeClusterChain(first_cluster);
                    return status;
                }

                src += to_write;
                remaining -= to_write;
            }

            cluster = FatNextCluster(cluster);
        }
    }

    // -----------------------------------------------------------------------
    // STEP 4: Update the directory entry.
    // -----------------------------------------------------------------------
    ntstatus status = STATUS_SUCCESS;
    if (found_existing) {
        status = FatWriteDirectoryEntries(entry_sector, entry_offset, NULL, short_name, FAT_ATTR_ARCHIVE, first_cluster, size);
    } else {
        u32 slots_needed = 1;
        if (FatNeedLfn(filename)) {
            slots_needed = ((u32)RtStrLen(filename) + 12) / 13 + 1;
        }

        u32 target_sector = 0;
        u32 target_offset = 0;
        status = FatFindFreeSlots(dir_cluster, slots_needed, &target_sector, &target_offset);
        if (status == STATUS_NOT_FOUND) {
            status = FatExpandDirectory(dir_cluster, &target_sector);
            target_offset = 0;
        }

        if (NT_SUCCESS(status)) {
            status = FatWriteDirectoryEntries(target_sector, target_offset, filename, short_name, FAT_ATTR_ARCHIVE, first_cluster, size);
        }
    }

    if (NT_ERROR(status)) {
        if (first_cluster) FatFreeClusterChain(first_cluster);
        return status;
    }

    KdPrintf("[FAT] WriteFile: %s cluster=%u size=%u ok\n", filename, first_cluster, size);
    return STATUS_SUCCESS;
}


ntstatus Fat32DeleteFile(const char* path) {
    if (!g_fat_initialized || !g_sector_buf) return STATUS_UNSUCCESSFUL;
    if (!path || !*path) return STATUS_INVALID_PARAMETER;

    const char* p = path;
    while (*p == '/') p++;
    if (*p == 0) return STATUS_INVALID_PARAMETER;

    const char* last_slash = NULL;
    const char* scan = p;
    while (*scan) { if (*scan == '/') last_slash = scan; scan++; }

    char parent_path[FAT_MAX_PATH];
    char filename[256];

    if (last_slash) {
        u32 parent_len = (u32)(last_slash - p);
        RtMemCopy(parent_path, p, parent_len);
        parent_path[parent_len] = 0;
        RtStrCopy(filename, last_slash + 1);
    } else {
        parent_path[0] = 0;
        RtStrCopy(filename, p);
    }

    if (filename[0] == 0) return STATUS_INVALID_PARAMETER;

    u32 dir_cluster;
    if (parent_path[0] == 0) {
        dir_cluster = (g_fat.type == FAT_TYPE_32) ? g_fat.root_cluster : 0;
    } else {
        ntstatus status = Fat32OpenPath(parent_path);
        if (NT_ERROR(status)) return STATUS_NOT_FOUND;
        dir_cluster = g_current_cluster;
        Fat32Close();
    }

    u32 entry_sector = 0;
    u32 entry_offset = 0;
    u32 file_cluster = 0;
    u32 lfn_start_sector = 0;
    u32 lfn_start_offset = 0;

    ntstatus status = FatFindEntry(dir_cluster, filename, &entry_sector, &entry_offset, &file_cluster, NULL, &lfn_start_sector, &lfn_start_offset);
    if (NT_ERROR(status)) return status;

    if (file_cluster >= 2) {
        FatFreeClusterChain(file_cluster);
    }

    if (lfn_start_sector >= 2) {
        u32 cur_sec = lfn_start_sector;
        u32 cur_off = lfn_start_offset;

        while (1) {
            status = FatReadSector(cur_sec, g_sector_buf);
            if (NT_ERROR(status)) return status;

            g_sector_buf[cur_off] = 0xE5;

            status = FatWriteSector(cur_sec, g_sector_buf);
            if (NT_ERROR(status)) return status;

            if (cur_sec == entry_sector && cur_off == entry_offset) {
                break;
            }

            cur_off += 32;
            if (cur_off >= g_fat.bytes_per_sector) {
                cur_off = 0;
                cur_sec++;
            }
        }
    } else {
        status = FatReadSector(entry_sector, g_sector_buf);
        if (NT_ERROR(status)) return status;

        g_sector_buf[entry_offset] = 0xE5;

        status = FatWriteSector(entry_sector, g_sector_buf);
        if (NT_ERROR(status)) return status;
    }

    KdPrintf("[FAT] DeleteFile: %s ok\n", filename);
    return STATUS_SUCCESS;
}

ntstatus Fat32CreateDirectory(const char* path) {
    if (!g_fat_initialized || !g_sector_buf) return STATUS_UNSUCCESSFUL;
    if (!path || !*path) return STATUS_INVALID_PARAMETER;

    const char* p = path;
    while (*p == '/') p++;
    if (*p == 0) return STATUS_INVALID_PARAMETER;

    {
        ntstatus exist_status = Fat32OpenPath(path);
        if (NT_SUCCESS(exist_status)) {
            Fat32Close();
            return STATUS_SUCCESS;
        }
    }

    usize plen = RtStrLen(p);
    while (plen > 1 && p[plen - 1] == '/') plen--;

    const char* last_slash = NULL;
    for (u32 i = 0; i < plen; i++) { if (p[i] == '/') last_slash = p + i; }

    char parent_path[FAT_MAX_PATH];
    char dirname[256];

    if (last_slash) {
        u32 parent_len = (u32)(last_slash - p);
        RtMemCopy(parent_path, p, parent_len);
        parent_path[parent_len] = 0;
        u32 name_len = (u32)(p + plen - last_slash - 1);
        RtMemCopy(dirname, last_slash + 1, name_len);
        dirname[name_len] = 0;
    } else {
        parent_path[0] = 0;
        u32 name_len = (u32)plen;
        RtMemCopy(dirname, p, name_len);
        dirname[name_len] = 0;
    }

    if (dirname[0] == 0) return STATUS_INVALID_PARAMETER;

    char short_name[11];
    FatGenerateShortName(dirname, short_name);

    KdPrintf("[FAT] CreateDirectory: path='%s' parent='%s' dir='%s'\n", path, parent_path, dirname);

    u32 dir_cluster;
    if (parent_path[0] == 0) {
        dir_cluster = (g_fat.type == FAT_TYPE_32) ? g_fat.root_cluster : 0;
    } else {
        ntstatus status = Fat32OpenPath(parent_path);
        if (NT_ERROR(status)) return STATUS_NOT_FOUND;
        dir_cluster = g_current_cluster;
        Fat32Close();
    }

    u32 new_cluster = FatAllocateCluster();
    if (new_cluster == 0) return STATUS_DISK_FULL;

    u32 sector = FatClusterToSector(new_cluster);
    RtMemSet(g_sector_buf, 0, g_fat.bytes_per_sector);

    u8* dot = g_sector_buf;
    RtMemSet(dot, ' ', 11);
    dot[0] = '.';
    dot[11] = FAT_ATTR_DIRECTORY;
    if (g_fat.type == FAT_TYPE_32) {
        *(u16*)(dot + 20) = (u16)(new_cluster >> 16);
    }
    *(u16*)(dot + 26) = (u16)(new_cluster & 0xFFFF);

    u8* dotdot = g_sector_buf + 32;
    RtMemSet(dotdot, ' ', 11);
    dotdot[0] = '.';
    dotdot[1] = '.';
    dotdot[11] = FAT_ATTR_DIRECTORY;
    if (g_fat.type == FAT_TYPE_32) {
        *(u16*)(dotdot + 20) = (u16)(dir_cluster >> 16);
    }
    *(u16*)(dotdot + 26) = (u16)(dir_cluster & 0xFFFF);

    ntstatus status = FatWriteSector(sector, g_sector_buf);
    if (NT_ERROR(status)) {
        FatFreeClusterChain(new_cluster);
        return status;
    }

    for (u32 s = 1; s < g_fat.sectors_per_cluster; s++) {
        RtMemSet(g_sector_buf, 0, g_fat.bytes_per_sector);
        FatWriteSector(sector + s, g_sector_buf);
    }

    u32 slots_needed = 1;
    if (FatNeedLfn(dirname)) {
        slots_needed = ((u32)RtStrLen(dirname) + 12) / 13 + 1;
    }

    u32 target_sector = 0;
    u32 target_offset = 0;
    status = FatFindFreeSlots(dir_cluster, slots_needed, &target_sector, &target_offset);
    if (status == STATUS_NOT_FOUND) {
        status = FatExpandDirectory(dir_cluster, &target_sector);
        target_offset = 0;
    }

    if (NT_SUCCESS(status)) {
        status = FatWriteDirectoryEntries(target_sector, target_offset, dirname, short_name, FAT_ATTR_DIRECTORY, new_cluster, 0);
    }

    if (NT_ERROR(status)) {
        FatFreeClusterChain(new_cluster);
        return status;
    }

    KdPrintf("[FAT] CreateDirectory: %s cluster=%u ok\n", dirname, new_cluster);
    return STATUS_SUCCESS;
}
