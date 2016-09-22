// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi.h>
#include <efilib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmdline.h>
#include <magenta.h>
#include <netboot.h>
#include <utils.h>

static EFI_GUID GraphicsOutputProtocol = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

#define DEFAULT_TIMEOUT 3

#define KBUFSIZE (32*1024*1024)
#define RBUFSIZE (256*1024*1024)

static nbfile nbkernel;
static nbfile nbramdisk;
static nbfile nbcmdline;

nbfile* netboot_get_buffer(const char* name) {
    // we know these are in a buffer large enough
    // that this is safe (todo: implement strcmp)
    if (!memcmp(name, "kernel.bin", 11)) {
        return &nbkernel;
    }
    if (!memcmp(name, "ramdisk.bin", 11)) {
        return &nbramdisk;
    }
    if (!memcmp(name, "cmdline", 7)) {
        return &nbcmdline;
    }
    return NULL;
}

static char cmdline[4096];

enum {
    BOOT_DEVICE_NONE,
    BOOT_DEVICE_NETBOOT,
    BOOT_DEVICE_LOCAL,
};

int boot_prompt(EFI_SYSTEM_TABLE* sys, int timeout_s) {
    EFI_BOOT_SERVICES* bs = sys->BootServices;

    EFI_EVENT TimerEvent;
    EFI_EVENT WaitList[2];

    EFI_STATUS status;
    UINTN Index;
    EFI_INPUT_KEY key;
    memset(&key, 0, sizeof(key));

    status = bs->CreateEvent(EVT_TIMER, 0, NULL, NULL, &TimerEvent);
    if (status != EFI_SUCCESS) {
        printf("could not create event timer: %s\n", efi_strerror(status));
        return BOOT_DEVICE_NONE;
    }

    status = bs->SetTimer(TimerEvent, TimerPeriodic, 10000000);
    if (status != EFI_SUCCESS) {
        printf("could not set timer: %s\n", efi_strerror(status));
        return BOOT_DEVICE_NONE;
    }

    int wait_idx = 0;
    int key_idx = wait_idx;
    WaitList[wait_idx++] = sys->ConIn->WaitForKey;
    int timer_idx = wait_idx;  // timer should always be last
    WaitList[wait_idx++] = TimerEvent;

    printf("Press (n) for netboot or (m) to boot the magenta.bin on the device\n");
    // TODO: better event loop
    do {
        status = bs->WaitForEvent(wait_idx, WaitList, &Index);

        // Check the timer
        if (!EFI_ERROR(status)) {
            if (Index == timer_idx) {
                printf(".");
                timeout_s--;
                continue;
            } else if (Index == key_idx) {
                status = sys->ConIn->ReadKeyStroke(sys->ConIn, &key);
                if (EFI_ERROR(status)) {
                    // clear the key and wait for another event
                    memset(&key, 0, sizeof(key));
                }
            }
        } else {
            printf("Error waiting for event: %s\n", efi_strerror(status));
            return BOOT_DEVICE_NONE;
        }
    } while ((key.UnicodeChar != 'n' && key.UnicodeChar != 'm') && timeout_s);
    printf("\n");

    bs->CloseEvent(TimerEvent);
    if (timeout_s > 0 || status == EFI_SUCCESS) {
        if (key.UnicodeChar == 'n') {
            return BOOT_DEVICE_NETBOOT;
        } else if (key.UnicodeChar == 'm') {
            return BOOT_DEVICE_LOCAL;
        }
    }

    // Default to netboot
    printf("Time out! Trying netboot...\n");
    return BOOT_DEVICE_NETBOOT;
}

void set_graphics_mode(EFI_SYSTEM_TABLE* sys, EFI_GRAPHICS_OUTPUT_PROTOCOL* gop, char* cmdline) {
    if (!gop || !cmdline) return;

    char res[11];
    if (cmdline_get(cmdline, "bootloader.fbres", res, sizeof(res)) < 0) return;

    uint32_t hres = 0;
    hres = atol(res);

    char* x = strchr(res, 'x');
    if (!x) return;
    x++;

    uint32_t vres = 0;
    vres = atol(x);
    if (!hres || !vres) return;

    UINT32 max_mode = gop->Mode->MaxMode;

    for (int i = 0; i < max_mode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* mode_info;
        UINTN info_size = 0;
        EFI_STATUS status = gop->QueryMode(gop, i, &info_size, &mode_info);
        if (EFI_ERROR(status)) {
            printf("Could not retrieve mode %d: %s\n", i, efi_strerror(status));
            continue;
        }

        if (mode_info->HorizontalResolution == hres &&
            mode_info->VerticalResolution == vres) {
            gop->SetMode(gop, i);
            sys->BootServices->Stall(1000);
            sys->ConOut->ClearScreen(sys->ConOut);
            return;
        }
    }
    printf("Could not find framebuffer mode %ux%u; using default mode = %ux%u\n",
            hres, vres, gop->Mode->Info->HorizontalResolution, gop->Mode->Info->VerticalResolution);
    sys->BootServices->Stall(5000000);
}

void draw_logo(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop) {
    if (!gop) return;

    const uint32_t h_res = gop->Mode->Info->HorizontalResolution;
    const uint32_t v_res = gop->Mode->Info->VerticalResolution;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL fuchsia = {
        .Red = 0xFF,
        .Green = 0x0,
        .Blue = 0xFF,
    };
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL black = {
        .Red = 0x0,
        .Green = 0x0,
        .Blue = 0x0
    };

    // Blank the screen, removing vendor UEFI logos
    gop->Blt(gop, &black, EfiBltVideoFill, 0, 0, 0, 0, h_res, v_res, 0);
    // Draw the Fuchsia square in the top right corner
    gop->Blt(gop, &fuchsia, EfiBltVideoFill, 0, 0, 0, 0, h_res, v_res/100, 0);
    gop->Blt(gop, &fuchsia, EfiBltVideoFill, 0, 0, 0, v_res - (v_res/100), h_res, v_res/100, 0);
}

void do_netboot(EFI_HANDLE img, EFI_SYSTEM_TABLE* sys) {
    EFI_BOOT_SERVICES* bs = sys->BootServices;

    EFI_PHYSICAL_ADDRESS mem = 0xFFFFFFFF;
    if (bs->AllocatePages(AllocateMaxAddress, EfiLoaderData, KBUFSIZE / 4096, &mem)) {
        printf("Failed to allocate network io buffer\n");
        return;
    }
    nbkernel.data = (void*) mem;
    nbkernel.size = KBUFSIZE;

    mem = 0xFFFFFFFF;
    if (bs->AllocatePages(AllocateMaxAddress, EfiLoaderData, RBUFSIZE / 4096, &mem)) {
        printf("Failed to allocate network io buffer\n");
        return;
    }
    nbramdisk.data = (void*) mem;
    nbramdisk.size = RBUFSIZE;

    nbcmdline.data = (void*) cmdline;
    nbcmdline.size = sizeof(cmdline);
    cmdline[0] = 0;

    printf("\nNetBoot Server Started...\n\n");
    EFI_TPL prev_tpl = bs->RaiseTPL(TPL_NOTIFY);
    for (;;) {
        int n = netboot_poll();
        if (n < 1) {
            continue;
        }
        if (nbkernel.offset < 32768) {
            // too small to be a kernel
            continue;
        }
        uint8_t* x = nbkernel.data;
        if ((x[0] == 'M') && (x[1] == 'Z') && (x[0x80] == 'P') && (x[0x81] == 'E')) {
            UINTN exitdatasize;
            EFI_STATUS r;
            EFI_HANDLE h;

            MEMMAP_DEVICE_PATH mempath[2] = {
                {
                    .Header = {
                        .Type = HARDWARE_DEVICE_PATH,
                        .SubType = HW_MEMMAP_DP,
                        .Length = { (UINT8)(sizeof(MEMMAP_DEVICE_PATH) & 0xff),
                            (UINT8)((sizeof(MEMMAP_DEVICE_PATH) >> 8) & 0xff), },
                    },
                    .MemoryType = EfiLoaderData,
                    .StartingAddress = (EFI_PHYSICAL_ADDRESS)nbkernel.data,
                    .EndingAddress = (EFI_PHYSICAL_ADDRESS)(nbkernel.data + nbkernel.offset),
                },
                {
                    .Header = {
                        .Type = END_DEVICE_PATH_TYPE,
                        .SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE,
                        .Length = { (UINT8)(sizeof(EFI_DEVICE_PATH) & 0xff),
                            (UINT8)((sizeof(EFI_DEVICE_PATH) >> 8) & 0xff), },
                    },
                },
            };

            printf("Attempting to run EFI binary...\n");
            r = bs->LoadImage(FALSE, img, (EFI_DEVICE_PATH*)mempath, (void*)nbkernel.data, nbkernel.offset, &h);
            if (EFI_ERROR(r)) {
                printf("LoadImage Failed (%s)\n", efi_strerror(r));
                continue;
            }
            r = bs->StartImage(h, &exitdatasize, NULL);
            if (EFI_ERROR(r)) {
                printf("StartImage Failed %ld\n", r);
                continue;
            }
            printf("\nNetBoot Server Resuming...\n");
            continue;
        }

        // make sure network traffic is not in flight, etc
        netboot_close();

        // Restore the TPL before booting the kernel, or failing to netboot
        bs->RestoreTPL(prev_tpl);

        // maybe it's a kernel image?
        EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
        bs->LocateProtocol(&GraphicsOutputProtocol, NULL, (void**)&gop);
        set_graphics_mode(sys, gop, cmdline);

        boot_kernel(img, sys, (void*) nbkernel.data, nbkernel.offset,
                    (void*) nbramdisk.data, nbramdisk.offset,
                    cmdline, sizeof(cmdline));
        break;
    }
}

EFI_STATUS efi_main(EFI_HANDLE img, EFI_SYSTEM_TABLE* sys) {
    EFI_BOOT_SERVICES* bs = sys->BootServices;

    InitializeLib(img, sys);
    InitGoodies(img, sys);

    // Load the cmdline
    UINTN csz = 0;
    char* cmdline = LoadFile(L"cmdline", &csz);
    if (cmdline) {
        cmdline[csz] = '\0';
        printf("cmdline: %s\n", cmdline);
    }

    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
    bs->LocateProtocol(&GraphicsOutputProtocol, NULL, (void**)&gop);
    set_graphics_mode(sys, gop, cmdline);
    draw_logo(gop);

    printf("\nOSBOOT v0.2\n\n");
    printf("Framebuffer base is at %lx\n\n", gop->Mode->FrameBufferBase);

    // See if there's a network interface
    bool have_network = netboot_init() == 0;

    // Look for a kernel image on disk
    // TODO: use the filesystem protocol
    UINTN ksz = 0;
    void* kernel = LoadFile(L"magenta.bin", &ksz);

    if (!have_network && kernel == NULL) {
        goto fail;
    }

    int boot_device = BOOT_DEVICE_NONE;
    if (have_network) {
        boot_device = BOOT_DEVICE_NETBOOT;
    }
    if (kernel != NULL) {
        if (boot_device != BOOT_DEVICE_NONE) {
            int timeout_s = cmdline_get_uint32(cmdline, "bootloader.timeout", DEFAULT_TIMEOUT);
            boot_device = boot_prompt(sys, timeout_s);
        } else {
            boot_device = BOOT_DEVICE_LOCAL;
        }
    }

    switch (boot_device) {
        case BOOT_DEVICE_NETBOOT:
            do_netboot(img, sys);
            break;
        case BOOT_DEVICE_LOCAL: {
            UINTN rsz = 0;
            void* ramdisk = LoadFile(L"ramdisk.bin", &rsz);
            boot_kernel(img, sys, kernel, ksz, ramdisk, rsz, cmdline, csz);
            break;
        }
        default:
            goto fail;
    }

fail:
    printf("\nBoot Failure\n");
    WaitAnyKey();
    return EFI_SUCCESS;
}
