#include "pe_loader.h"
#include "fat32.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "process.h"
#include "debug.h"
#include "runtime.h"
#include "hal.h" 

#define PE_DOS_MAGIC    0x5A4D
#define PE_SIGNATURE    0x00004550
#define PE32P_MAGIC     0x020B

#pragma pack(push, 1)
typedef struct {
    u16 e_magic;
    u16 e_cblp;
    u16 e_cp;
    u16 e_crlc;
    u16 e_cparhdr;
    u16 e_minalloc;
    u16 e_maxalloc;
    u16 e_ss;
    u16 e_sp;
    u16 e_csum;
    u16 e_ip;
    u16 e_cs;
    u16 e_lfarlc;
    u16 e_ovno;
    u16 e_res[4];
    u16 e_oemid;
    u16 e_oeminfo;
    u16 e_res2[10];
    u32 e_lfanew;
} PEDosHeader;

typedef struct {
    u16 Machine;
    u16 NumberOfSections;
    u32 TimeDateStamp;
    u32 PointerToSymbolTable;
    u32 NumberOfSymbols;
    u16 SizeOfOptionalHeader;
    u16 Characteristics;
} PEFileHeader;

typedef struct {
    u16 Magic;
    u8  MajorLinkerVersion;
    u8  MinorLinkerVersion;
    u32 SizeOfCode;
    u32 SizeOfInitializedData;
    u32 SizeOfUninitializedData;
    u32 AddressOfEntryPoint;
    u32 BaseOfCode;
    u64 ImageBase;
    u32 SectionAlignment;
    u32 FileAlignment;
    u16 MajorOperatingSystemVersion;
    u16 MinorOperatingSystemVersion;
    u16 MajorImageVersion;
    u16 MinorImageVersion;
    u16 MajorSubsystemVersion;
    u16 MinorSubsystemVersion;
    u32 Win32VersionValue;
    u32 SizeOfImage;
    u32 SizeOfHeaders;
    u32 CheckSum;
    u16 Subsystem;
    u16 DllCharacteristics;
    u64 SizeOfStackReserve;
    u64 SizeOfStackCommit;
    u64 SizeOfHeapReserve;
    u64 SizeOfHeapCommit;
    u32 LoaderFlags;
    u32 NumberOfRvaAndSizes;
    u64 DataDirectory[16];
} PEOptionalHeader64;

typedef struct {
    u8  Name[8];
    u32 VirtualSize;
    u32 VirtualAddress;
    u32 SizeOfRawData;
    u32 PointerToRawData;
    u32 PointerToRelocations;
    u32 PointerToLinenumbers;
    u16 NumberOfRelocations;
    u16 NumberOfLinenumbers;
    u32 Characteristics;
} PESectionHeader;
#pragma pack(pop)

ntstatus PeLoadProgram(const char* path, KProcess** out_process) {
    u32 file_size = 0;
    void* file_buffer = NULL;
    i32 free_buffer_after = 1;


    if (!file_buffer) {
        ntstatus status = Fat32OpenPath(path);
        if (NT_ERROR(status)) {
            KdPrintf("[PE] File not found: %s\n", path);
            return status;
        }
        file_size = Fat32GetFileSize();
        if (file_size == 0) {
            KdPrintf("[PE] File is empty: %s\n", path);
            Fat32Close();
            return STATUS_UNSUCCESSFUL;
        }
        file_buffer = KmAlloc(file_size);
        if (!file_buffer) {
            KdPrintf("[PE] Out of memory for file buffer\n");
            Fat32Close();
            return STATUS_OUT_OF_MEMORY;
        }
        u32 bytes_read = 0;
        status = Fat32ReadFile(file_buffer, &bytes_read);
        if (NT_ERROR(status) || bytes_read != file_size) {
            KdPrintf("[PE] Failed to read file from FAT: %s\n", path);
            KmFree(file_buffer);
            Fat32Close();
            return status;
        }
        Fat32Close();
    }

    PEDosHeader* dos = (PEDosHeader*)file_buffer;
    if (dos->e_magic != PE_DOS_MAGIC) {
        KdPrintf("[PE] Invalid DOS magic: 0x%04x\n", dos->e_magic);
        KmFree(file_buffer);
        return STATUS_UNSUCCESSFUL;
    }

    u32 pe_sig = *(u32*)((u8*)file_buffer + dos->e_lfanew);
    if (pe_sig != PE_SIGNATURE) {
        KdPrintf("[PE] Invalid PE signature: 0x%08x\n", pe_sig);
        KmFree(file_buffer);
        return STATUS_UNSUCCESSFUL;
    }

    PEFileHeader* file_hdr = (PEFileHeader*)((u8*)file_buffer + dos->e_lfanew + 4);
    PEOptionalHeader64* opt = (PEOptionalHeader64*)((u8*)file_hdr + sizeof(PEFileHeader));

    if (opt->Magic != PE32P_MAGIC) {
        KdPrintf("[PE] Not PE32+: magic=0x%04x\n", opt->Magic);
        KmFree(file_buffer);
        return STATUS_UNSUCCESSFUL;
    }

    KdPrintf("[PE] Loading: base=0x%llx size=%u entry=0x%x sections=%u\n",
             opt->ImageBase, opt->SizeOfImage, opt->AddressOfEntryPoint,
             file_hdr->NumberOfSections);

    KProcess* proc = PsCreateProcess(path,
                                     opt->ImageBase + opt->AddressOfEntryPoint,
                                     opt->ImageBase,
                                     opt->SizeOfImage);
    if (!proc) {
        KdPrintf("[PE] Failed to create process\n");
        KmFree(file_buffer);
        return STATUS_OUT_OF_MEMORY;
    }

    u64* pml4 = (u64*)PHYS_TO_VIRT(proc->page_table);
    u64 num_pages = (opt->SizeOfImage + PAGE_SIZE - 1) / PAGE_SIZE;

    for (u64 i = 0; i < num_pages; i++) {
        u64 phys = PmmAllocPage();
        if (!phys) {
            KdPrintf("[PE] Out of memory mapping program pages\n");
            PsDestroyProcess(proc);
            KmFree(file_buffer);
            return STATUS_OUT_OF_MEMORY;
        }
        RtMemSet((void*)PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
        u64 virt = opt->ImageBase + i * PAGE_SIZE;
        VmmMapPage(pml4, virt, phys, VMM_USER_FLAGS);
    }

    u64 first_page_phys = VmmGetPhysical(pml4, opt->ImageBase);
    RtMemCopy((void*)PHYS_TO_VIRT(first_page_phys), file_buffer, opt->SizeOfHeaders);

    PESectionHeader* sections = (PESectionHeader*)
        ((u8*)opt + file_hdr->SizeOfOptionalHeader);

    for (i32 i = 0; i < file_hdr->NumberOfSections; i++) {
        if (sections[i].SizeOfRawData == 0) {
            KdPrintf("[PE]   Section[%d]: VA=0x%x VSize=%u (BSS, zero-filled)\n",
                     i, sections[i].VirtualAddress, sections[i].VirtualSize);
            continue;
        }

        u64 sec_virt = opt->ImageBase + sections[i].VirtualAddress;
        u32 raw_off = sections[i].PointerToRawData;
        u32 remaining = sections[i].SizeOfRawData;
        u64 page_va = sec_virt & ~0xFFFULL;
        u64 page_off = sec_virt & 0xFFF;

        KdPrintf("[PE]   Section[%d]: VA=0x%x RawSize=%u VSize=%u '%8s' -> virt 0x%llx\n",
                 i, sections[i].VirtualAddress, sections[i].SizeOfRawData,
                 sections[i].VirtualSize, sections[i].Name, sec_virt);

        while (remaining > 0) {
            u64 page_phys = VmmGetPhysical(pml4, page_va);
            if (!page_phys) {
                KdPrintf("[PE] ERROR: page VA=0x%llx not mapped!\n", page_va);
                break;
            }
            u32 chunk = PAGE_SIZE - (u32)page_off;
            if (chunk > remaining) chunk = remaining;
            RtMemCopy((void*)PHYS_TO_VIRT(page_phys + page_off),
                      (u8*)file_buffer + raw_off,
                      chunk);
            raw_off += chunk;
            remaining -= chunk;
            page_va += PAGE_SIZE;
            page_off = 0;
        }
    }

    if (first_page_phys) {
        VmmMapPage(pml4, opt->ImageBase, first_page_phys, VMM_PRESENT | VMM_USER | VMM_NX);
    }
    for (i32 i = 0; i < file_hdr->NumberOfSections; i++) {
        u64 sec_virt = opt->ImageBase + sections[i].VirtualAddress;
        u64 sec_size = sections[i].VirtualSize ? sections[i].VirtualSize : sections[i].SizeOfRawData;
        u64 num_sec_pages = (sec_size + PAGE_SIZE - 1) / PAGE_SIZE;
        u64 flags = VMM_PRESENT | VMM_USER;
        if (sections[i].Characteristics & 0x80000000) flags |= VMM_WRITABLE;
        if (!(sections[i].Characteristics & 0x20000000)) flags |= VMM_NX;
        
        for (u64 p = 0; p < num_sec_pages; p++) {
            u64 va = sec_virt + p * PAGE_SIZE;
            u64 pa = VmmGetPhysical(pml4, va);
            if (pa) {
                VmmMapPage(pml4, va, pa, flags);
            }
        }
    }

    KdPrintf("[PE] Data directories (%u entries):\n", opt->NumberOfRvaAndSizes);
    {
        static const char* dir_names[] = {
            "Export", "Import", "Resource", "Exception",
            "Security", "BaseReloc", "Debug", "Architecture",
            "GlobalPtr", "TLS", "LoadConfig", "BoundImport",
            "IAT", "DelayImport", "CLR", "Reserved"
        };
        for (i32 d = 0; d < 16 && d < (i32)opt->NumberOfRvaAndSizes; d++) {
            u32 dd_rva = (u32)(opt->DataDirectory[d] & 0xFFFFFFFF);
            u32 dd_size = (u32)(opt->DataDirectory[d] >> 32);
            if (dd_rva || dd_size) {
                KdPrintf("[PE]   [%d] %s: RVA=0x%x Size=%u\n",
                         d, (d < 16 ? dir_names[d] : "?"), dd_rva, dd_size);
            }
        }
    }

    u32 import_rva = (u32)(opt->DataDirectory[1] & 0xFFFFFFFF);
    u32 import_size = (u32)(opt->DataDirectory[1] >> 32);
    if (import_rva && import_size) {
        KdPrintf("[PE] WARNING: Import table found! RVA=0x%x Size=%u\n", import_rva, import_size);
        KdPrintf("[PE] WARNING: Imports are NOT resolved - IAT entries will be invalid!\n");

        u64 import_file_off = 0;
        for (i32 s = 0; s < file_hdr->NumberOfSections; s++) {
            u32 sva = sections[s].VirtualAddress;
            u32 svs = sections[s].VirtualSize ? sections[s].VirtualSize : sections[s].SizeOfRawData;
            if (import_rva >= sva && import_rva < sva + svs) {
                import_file_off = sections[s].PointerToRawData + (import_rva - sva);
                break;
            }
        }
        if (import_file_off) {
            u8* imp_ptr = (u8*)file_buffer + import_file_off;
            for (i32 idx = 0; idx < 32; idx++) {
                u32 ilt_rva = *(u32*)(imp_ptr + idx * 20 + 0);
                u32 name_rva = *(u32*)(imp_ptr + idx * 20 + 12);
                u32 iat_rva = *(u32*)(imp_ptr + idx * 20 + 16);
                if (!ilt_rva && !iat_rva) break;

                u64 dll_name_off = 0;
                if (name_rva) {
                    for (i32 s = 0; s < file_hdr->NumberOfSections; s++) {
                        u32 sva = sections[s].VirtualAddress;
                        u32 svsc = sections[s].SizeOfRawData;
                        if (name_rva >= sva && name_rva < sva + svsc) {
                            dll_name_off = sections[s].PointerToRawData + (name_rva - sva);
                            break;
                        }
                    }
                }
                if (dll_name_off) {
                    const char* dll_name = (const char*)((u8*)file_buffer + dll_name_off);
                    KdPrintf("[PE]   Import[%d]: DLL='%s' ILT=0x%x IAT=0x%x\n",
                             idx, dll_name, ilt_rva, iat_rva);
                } else {
                    KdPrintf("[PE]   Import[%d]: NameRVA=0x%x ILT=0x%x IAT=0x%x\n",
                             idx, name_rva, ilt_rva, iat_rva);
                }
            }
        }
    }

    u32 iat_rva = (u32)(opt->DataDirectory[12] & 0xFFFFFFFF);
    u32 iat_size = (u32)(opt->DataDirectory[12] >> 32);
    if (iat_rva && iat_size) {
        KdPrintf("[PE] IAT: RVA=0x%x Size=%u (%u entries)\n",
                 iat_rva, iat_size, iat_size / 8);
    }

    KdPrintf("[PE] Verifying .data section load (first 32 bytes at section start):\n");
    {
        u64 data_start = opt->ImageBase + sections[1].VirtualAddress;
        u64 data_phys = VmmGetPhysical(pml4, data_start);
        if (data_phys) {
            u8* dp = (u8*)PHYS_TO_VIRT(data_phys);
            KdPrintf("[PE]   ");
            for (i32 b = 0; b < 32; b++) KdPrintf("%02x ", dp[b]);
            KdPrintf("\n");
        }
    }

    KThread* main_thread = PsCreateThread(proc, opt->ImageBase + opt->AddressOfEntryPoint, 0);
    if (!main_thread) {
        KdPrintf("[PE] Failed to create main thread\n");
        PsDestroyProcess(proc);
        KmFree(file_buffer);
        return STATUS_OUT_OF_MEMORY;
    }

    main_thread->context.rflags = 0x202;
    main_thread->context.cr3 = proc->page_table;

    if (free_buffer_after) {
        KmFree(file_buffer);
    }

    KdPrintf("[PE] Program loaded: PID=%llu entry=0x%llx stack=0x%llx\n",
             proc->pid, opt->ImageBase + opt->AddressOfEntryPoint, main_thread->context.rsp);

    *out_process = proc;
    return STATUS_SUCCESS;
}
