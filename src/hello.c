// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi.h>

EFI_STATUS efi_main(EFI_HANDLE img, EFI_SYSTEM_TABLE* sys) {
    SIMPLE_TEXT_OUTPUT_INTERFACE* ConOut = sys->ConOut;
    ConOut->OutputString(ConOut, L"Hello, EFI World!\r\n");
    return EFI_SUCCESS;
}
