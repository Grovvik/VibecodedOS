#include <efi.h>
#include <efilib.h>
#include "types.h"
#include "boot_info.h"


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
} PE_DOS_HEADER;

typedef struct {
    u16 Machine;
    u16 NumberOfSections;
    u32 TimeDateStamp;
    u32 PointerToSymbolTable;
    u32 NumberOfSymbols;
    u16 SizeOfOptionalHeader;
    u16 Characteristics;
} PE_FILE_HEADER;

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
} PE_OPTIONAL_HEADER64;

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
} PE_SECTION_HEADER;
#pragma pack(pop)

#define PE_DOS_MAGIC    0x5A4D
#define PE_SIGNATURE    0x00004550
#define PE32P_MAGIC     0x020B

static void BootMemSet(void* dst, u8 val, usize count) {
    u8* d = (u8*)dst;
    while (count--) *d++ = val;
}

static void BootMemCopy(void* dst, const void* src, usize count) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    while (count--) *d++ = *s++;
}

static UINTN BootStrLen(const CHAR16* s) {
    UINTN len = 0;
    while (*s++) len++;
    return len;
}

static EFI_STATUS GetFrameBufferInfo(BootInfo* bi) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
    EFI_STATUS status = uefi_call_wrapper(BS->LocateProtocol, 3,
        &gEfiGraphicsOutputProtocolGuid, NULL, (void**)&gop);
    if (EFI_ERROR(status)) {
        Print(L"  ERROR: GOP not found: %r\n", status);
        return status;
    }

    bi->fb_base   = gop->Mode->FrameBufferBase;
    bi->fb_size   = gop->Mode->FrameBufferSize;
    bi->fb_width  = gop->Mode->Info->HorizontalResolution;
    bi->fb_height = gop->Mode->Info->VerticalResolution;
    bi->fb_pitch  = gop->Mode->Info->PixelsPerScanLine * 4;
    bi->fb_bpp    = 32;

    Print(L"  Framebuffer: %ux%u @ 0x%lx, %u bytes, pitch=%u, %ubpp\n",
          bi->fb_width, bi->fb_height, bi->fb_base,
          (UINTN)bi->fb_size, bi->fb_pitch, bi->fb_bpp);

    return EFI_SUCCESS;
}

static EFI_STATUS LoadKernel(EFI_HANDLE ImageHandle, BootInfo* bi) {
    EFI_LOADED_IMAGE* loaded_image;
    EFI_STATUS status = uefi_call_wrapper(BS->OpenProtocol, 6,
        ImageHandle, &gEfiLoadedImageProtocolGuid,
        (void**)&loaded_image, ImageHandle, NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(status)) {
        Print(L"  ERROR: OpenProtocol(LoadedImage): %r\n", status);
        return status;
    }

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;
    status = uefi_call_wrapper(BS->OpenProtocol, 6,
        loaded_image->DeviceHandle,
        &gEfiSimpleFileSystemProtocolGuid,
        (void**)&fs, ImageHandle, NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(status)) {
        Print(L"  ERROR: OpenProtocol(SimpleFileSystem): %r\n", status);
        return status;
    }

    EFI_FILE_HANDLE root;
    status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
    if (EFI_ERROR(status)) {
        Print(L"  ERROR: OpenVolume: %r\n", status);
        return status;
    }

    EFI_FILE_HANDLE kernel_file;
    status = uefi_call_wrapper(root->Open, 5,
        root, &kernel_file, L"\\EFI\\OS\\Kernel.exe",
        EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        Print(L"  ERROR: Kernel.exe not found: %r\n", status);
        uefi_call_wrapper(root->Close, 1, root);
        return status;
    }

    UINTN info_size = sizeof(EFI_FILE_INFO) + 512;
    EFI_FILE_INFO* file_info = (EFI_FILE_INFO*)AllocatePool(info_size);
    if (!file_info) {
        uefi_call_wrapper(kernel_file->Close, 1, kernel_file);
        uefi_call_wrapper(root->Close, 1, root);
        return EFI_OUT_OF_RESOURCES;
    }

    status = uefi_call_wrapper(kernel_file->GetInfo, 4,
        kernel_file, &gEfiFileInfoGuid, &info_size, file_info);
    if (EFI_ERROR(status)) {
        Print(L"  ERROR: GetInfo: %r\n", status);
        FreePool(file_info);
        uefi_call_wrapper(kernel_file->Close, 1, kernel_file);
        uefi_call_wrapper(root->Close, 1, root);
        return status;
    }

    UINTN file_size = (UINTN)file_info->FileSize;
    FreePool(file_info);
    Print(L"  Kernel file size: %lu bytes\n", file_size);

    void* file_buffer = AllocatePool(file_size);
    if (!file_buffer) {
        uefi_call_wrapper(kernel_file->Close, 1, kernel_file);
        uefi_call_wrapper(root->Close, 1, root);
        return EFI_OUT_OF_RESOURCES;
    }

    UINTN read_size = file_size;
    status = uefi_call_wrapper(kernel_file->Read, 3,
        kernel_file, &read_size, file_buffer);
    uefi_call_wrapper(kernel_file->Close, 1, kernel_file);
    uefi_call_wrapper(root->Close, 1, root);

    if (EFI_ERROR(status)) {
        Print(L"  ERROR: Read kernel: %r\n", status);
        FreePool(file_buffer);
        return status;
    }

    Print(L"  Parsing PE headers...\n");

    PE_DOS_HEADER* dos = (PE_DOS_HEADER*)file_buffer;
    if (dos->e_magic != PE_DOS_MAGIC) {
        Print(L"  ERROR: Invalid DOS magic 0x%04x\n", dos->e_magic);
        FreePool(file_buffer);
        return EFI_LOAD_ERROR;
    }

    u32 pe_sig = *(u32*)((u8*)file_buffer + dos->e_lfanew);
    if (pe_sig != PE_SIGNATURE) {
        Print(L"  ERROR: Invalid PE signature 0x%08x\n", pe_sig);
        FreePool(file_buffer);
        return EFI_LOAD_ERROR;
    }

    PE_FILE_HEADER* file_hdr = (PE_FILE_HEADER*)((u8*)file_buffer + dos->e_lfanew + 4);
    PE_OPTIONAL_HEADER64* opt = (PE_OPTIONAL_HEADER64*)((u8*)file_hdr + sizeof(PE_FILE_HEADER));

    if (opt->Magic != PE32P_MAGIC) {
        Print(L"  ERROR: Not PE32+ (magic=0x%04x)\n", opt->Magic);
        FreePool(file_buffer);
        return EFI_LOAD_ERROR;
    }

    Print(L"  ImageBase=0x%lx SizeOfImage=%lu EntryPoint=0x%x Sections=%u\n",
          opt->ImageBase, opt->SizeOfImage, opt->AddressOfEntryPoint,
          file_hdr->NumberOfSections);

    EFI_PHYSICAL_ADDRESS alloc_addr = opt->ImageBase;
    u64 num_pages = (opt->SizeOfImage + 4095) / 4096;

    status = uefi_call_wrapper(BS->AllocatePages, 4,
        AllocateAddress, EfiLoaderData, num_pages, &alloc_addr);

    if (EFI_ERROR(status)) {
        Print(L"  AllocateAddress at 0x%lx failed (%r), trying alternatives...\n",
              opt->ImageBase, status);

        u64 try_addrs[] = { 0x200000, 0x400000, 0x800000, 0x1000000, 0x2000000, 0x4000000 };
        for (int i = 0; i < 6; i++) {
            alloc_addr = try_addrs[i];
            status = uefi_call_wrapper(BS->AllocatePages, 4,
                AllocateAddress, EfiLoaderData, num_pages, &alloc_addr);
            if (!EFI_ERROR(status)) break;
        }

        if (EFI_ERROR(status)) {
            alloc_addr = 0xFFFFFFFFFFFFFFFFULL;
            status = uefi_call_wrapper(BS->AllocatePages, 4,
                AllocateMaxAddress, EfiLoaderData, num_pages, &alloc_addr);
        }

        if (EFI_ERROR(status)) {
            Print(L"  ERROR: Cannot allocate kernel memory: %r\n", status);
            FreePool(file_buffer);
            return status;
        }

        Print(L"  Kernel allocated at 0x%lx (different from ImageBase 0x%lx)\n",
              alloc_addr, opt->ImageBase);
    }

    u64 load_base = alloc_addr;

    BootMemSet((void*)(usize)load_base, 0, opt->SizeOfImage);

    BootMemCopy((void*)(usize)load_base,
                (u8*)file_buffer,
                opt->SizeOfHeaders);

    PE_SECTION_HEADER* sections = (PE_SECTION_HEADER*)
        ((u8*)opt + file_hdr->SizeOfOptionalHeader);

    for (int i = 0; i < file_hdr->NumberOfSections; i++) {
        if (sections[i].SizeOfRawData == 0) continue;

        void* dst = (void*)(usize)(load_base + sections[i].VirtualAddress);
        void* src = (void*)((u8*)file_buffer + sections[i].PointerToRawData);

        Print(L"  Section[%d]: VA=0x%x Size=%u -> 0x%lx\n",
              i, sections[i].VirtualAddress, sections[i].SizeOfRawData,
              load_base + sections[i].VirtualAddress);

        BootMemCopy(dst, src, sections[i].SizeOfRawData);
    }

    bi->kernel_image_base   = load_base;
    bi->kernel_entry_point  = load_base + opt->AddressOfEntryPoint;
    bi->kernel_image_size   = opt->SizeOfImage;

    Print(L"  Kernel loaded: base=0x%llx entry=0x%llx size=%llu\n",
          bi->kernel_image_base, bi->kernel_entry_point, bi->kernel_image_size);

    FreePool(file_buffer);
    return EFI_SUCCESS;
}

static void GetRSDP(BootInfo* bi) {
    bi->rsdp = 0;

    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
        EFI_GUID acpi20 = ACPI_20_TABLE_GUID;
        EFI_GUID acpi10 = ACPI_TABLE_GUID;

        if (CompareGuid(&ST->ConfigurationTable[i].VendorGuid, &acpi20) == 0) {
            bi->rsdp = (u64)(usize)ST->ConfigurationTable[i].VendorTable;
            Print(L"  RSDP (ACPI 2.0) found at 0x%llx\n", bi->rsdp);
            return;
        }
        if (CompareGuid(&ST->ConfigurationTable[i].VendorGuid, &acpi10) == 0) {
            if (bi->rsdp == 0) {
                bi->rsdp = (u64)(usize)ST->ConfigurationTable[i].VendorTable;
                Print(L"  RSDP (ACPI 1.0) found at 0x%llx\n", bi->rsdp);
            }
        }
    }

    if (bi->rsdp == 0) {
        Print(L"  WARNING: RSDP not found in EFI config table\n");
    }
}

static EFI_STATUS ExitBootServicesAndJump(EFI_HANDLE ImageHandle, BootInfo* bi) {
    UINTN map_size = 0, map_key, desc_size;
    EFI_MEMORY_DESCRIPTOR* map = NULL;
    UINT32 desc_version;

    EFI_STATUS status = uefi_call_wrapper(BS->GetMemoryMap, 5,
        &map_size, map, NULL, &desc_size, NULL);

    map_size += 2 * desc_size;

    status = uefi_call_wrapper(BS->AllocatePool, 3,
        EfiLoaderData, map_size, (void**)&map);
    if (EFI_ERROR(status)) return status;

    status = uefi_call_wrapper(BS->GetMemoryMap, 5,
        &map_size, map, &map_key, &desc_size, &desc_version);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(BS->FreePool, 1, map);
        return status;
    }

    status = uefi_call_wrapper(BS->ExitBootServices, 2,
        ImageHandle, map_key);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(BS->FreePool, 1, map);
        return status;
    }

    bi->memory_map          = map;
    bi->memory_map_size     = map_size;
    bi->memory_map_entry_size = desc_size;
    bi->memory_map_entry_count = map_size / desc_size;

    bi->total_usable_memory = 0;
    UINTN num_entries = map_size / desc_size;
    for (UINTN i = 0; i < num_entries; i++) {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)
            ((u8*)map + i * desc_size);
        if (desc->Type == EfiConventionalMemory ||
            desc->Type == EfiBootServicesCode ||
            desc->Type == EfiBootServicesData) {
            bi->total_usable_memory += desc->NumberOfPages * 4096;
        }
    }

    return EFI_SUCCESS;
}

static void JumpToKernel(BootInfo* bi) {
    void (*entry)(BootInfo*) = (void(*)(BootInfo*))(usize)bi->kernel_entry_point;
    entry(bi);

    while (1);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    Print(L"\n=== MicroNT Bootloader v0.1 ===\n\n");

    BootInfo boot_info;
    BootMemSet(&boot_info, 0, sizeof(boot_info));

    Print(L"[1/3] Getting framebuffer info...\n");
    EFI_STATUS status = GetFrameBufferInfo(&boot_info);
    if (EFI_ERROR(status)) {
        Print(L"FATAL: Cannot get framebuffer: %r\n", status);
        return status;
    }

    Print(L"[2/3] Loading kernel from ESP:\\EFI\\OS\\Kernel.exe...\n");
    status = LoadKernel(ImageHandle, &boot_info);
    if (EFI_ERROR(status)) {
        Print(L"FATAL: Cannot load kernel: %r\n", status);
        return status;
    }

    Print(L"[3/3] Finding RSDP...\n");
    GetRSDP(&boot_info);

    status = ExitBootServicesAndJump(ImageHandle, &boot_info);
    if (EFI_ERROR(status)) {
        Print(L"FATAL: ExitBootServices failed: %r\n", status);
        return status;
    }

    JumpToKernel(&boot_info);

    return EFI_SUCCESS;
}
