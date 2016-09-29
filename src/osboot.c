// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <efi/boot-services.h>
#include <efi/system-table.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/graphics-output.h>
#include <efi/protocol/simple-text-input.h>

#include <cmdline.h>
#include <magenta.h>
#include <netboot.h>
#include <utils.h>

#define DEFAULT_TIMEOUT 3

#define KBUFSIZE (32*1024*1024)
#define RBUFSIZE (256*1024*1024)

static char cmdextra[256];

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

int boot_prompt(efi_system_table* sys, int timeout_s) {
    efi_boot_services* bs = sys->BootServices;

    efi_event TimerEvent;
    efi_event WaitList[2];

    efi_status status;
    size_t Index;
    efi_input_key key;
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

void set_graphics_mode(efi_system_table* sys, efi_graphics_output_protocol* gop, char* cmdline) {
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

    uint32_t max_mode = gop->Mode->MaxMode;

    for (int i = 0; i < max_mode; i++) {
        efi_graphics_output_mode_information* mode_info;
        size_t info_size = 0;
        efi_status status = gop->QueryMode(gop, i, &info_size, &mode_info);
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

void draw_logo(efi_graphics_output_protocol* gop) {
    if (!gop) return;

    const uint32_t h_res = gop->Mode->Info->HorizontalResolution;
    const uint32_t v_res = gop->Mode->Info->VerticalResolution;
    efi_graphics_output_blt_pixel fuchsia = {
        .Red = 0xFF,
        .Green = 0x0,
        .Blue = 0xFF,
    };
    efi_graphics_output_blt_pixel black = {
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

void do_netboot(efi_handle img, efi_system_table* sys) {
    efi_boot_services* bs = sys->BootServices;

    efi_physical_addr mem = 0xFFFFFFFF;
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
    nbcmdline.size = sizeof(cmdline) - 1;
    nbcmdline.offset = 0;
    cmdline[0] = 0;

    printf("\nNetBoot Server Started...\n\n");
    efi_tpl prev_tpl = bs->RaiseTPL(TPL_NOTIFY);
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
            size_t exitdatasize;
            efi_status r;
            efi_handle h;

            efi_device_path_hw_memmap mempath[2] = {
                {
                    .Header = {
                        .Type = DEVICE_PATH_HARDWARE,
                        .SubType = DEVICE_PATH_HW_MEMMAP,
                        .Length = { (uint8_t)(sizeof(efi_device_path_hw_memmap) & 0xff),
                            (uint8_t)((sizeof(efi_device_path_hw_memmap) >> 8) & 0xff), },
                    },
                    .MemoryType = EfiLoaderData,
                    .StartAddress = (efi_physical_addr)nbkernel.data,
                    .EndAddress = (efi_physical_addr)(nbkernel.data + nbkernel.offset),
                },
                {
                    .Header = {
                        .Type = DEVICE_PATH_END,
                        .SubType = DEVICE_PATH_ENTIRE_END,
                        .Length = { (uint8_t)(sizeof(efi_device_path_protocol) & 0xff),
                            (uint8_t)((sizeof(efi_device_path_protocol) >> 8) & 0xff), },
                    },
                },
            };

            printf("Attempting to run EFI binary...\n");
            r = bs->LoadImage(false, img, (efi_device_path_protocol*)mempath, (void*)nbkernel.data, nbkernel.offset, &h);
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

        // ensure cmdline is null terminated
        cmdline[nbcmdline.offset] = 0;

        // maybe it's a kernel image?
        efi_graphics_output_protocol* gop;
        bs->LocateProtocol(&GraphicsOutputProtocol, NULL, (void**)&gop);
        set_graphics_mode(sys, gop, cmdline);

        boot_kernel(img, sys, (void*) nbkernel.data, nbkernel.offset,
                    (void*) nbramdisk.data, nbramdisk.offset,
                    cmdline, strlen(cmdline), cmdextra, strlen(cmdextra));
        break;
    }
}

EFIAPI efi_status efi_main(efi_handle img, efi_system_table* sys) {
    efi_boot_services* bs = sys->BootServices;

    InitGoodies(img, sys);

    uint64_t mmio;
    if (FindPCIMMIO(bs, 0x0C, 0x03, 0x30, &mmio) == EFI_SUCCESS) {
        sprintf(cmdextra, " xdc.mmio=0x%lx ", mmio);
    } else {
        cmdextra[0] = 0;
    }

    // Load the cmdline
    size_t csz = 0;
    char* cmdline = LoadFile(L"cmdline", &csz);
    if (cmdline) {
        cmdline[csz] = '\0';
        printf("cmdline: %s\n", cmdline);
    }

    efi_graphics_output_protocol* gop;
    bs->LocateProtocol(&GraphicsOutputProtocol, NULL, (void**)&gop);
    set_graphics_mode(sys, gop, cmdline);
    draw_logo(gop);

    printf("\nOSBOOT v0.2\n\n");
    printf("Framebuffer base is at %lx\n\n", gop->Mode->FrameBufferBase);

    // See if there's a network interface
    bool have_network = netboot_init() == 0;

    // Look for a kernel image on disk
    // TODO: use the filesystem protocol
    size_t ksz = 0;
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
            size_t rsz = 0;
            void* ramdisk = LoadFile(L"ramdisk.bin", &rsz);
            boot_kernel(img, sys, kernel, ksz, ramdisk, rsz,
                        cmdline, csz, cmdextra, strlen(cmdextra));
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
