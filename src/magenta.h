// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi.h>

typedef struct {
    UINT8* zeropage;
    UINT8* cmdline;
    void* image;
    UINT32 pages;
} kernel_t;

int boot_kernel(EFI_HANDLE img, EFI_SYSTEM_TABLE* sys,
                void* image, size_t sz, void* ramdisk, size_t rsz,
                void* cmdline, size_t csz, void* cmdline2, size_t csz2);
