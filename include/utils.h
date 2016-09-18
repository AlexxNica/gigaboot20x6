// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

void InitGoodies(EFI_HANDLE img, EFI_SYSTEM_TABLE* sys);

void WaitAnyKey(void);
void Fatal(const char* msg, EFI_STATUS status);
CHAR16* HandleToString(EFI_HANDLE handle);
const char *efi_strerror(EFI_STATUS status);
const CHAR16 *efi_wstrerror(EFI_STATUS status);
size_t strlen_16(CHAR16 *str);

// Convenience wrappers for Open/Close protocol for use by
// UEFI app code that's not a driver model participant
EFI_STATUS OpenProtocol(EFI_HANDLE h, EFI_GUID* guid, void** ifc);
EFI_STATUS CloseProtocol(EFI_HANDLE h, EFI_GUID* guid);

void* LoadFile(CHAR16* filename, UINTN* size_out);

// GUIDs
extern EFI_GUID SimpleFileSystemProtocol;
extern EFI_GUID FileInfoGUID;

// Global Context
extern EFI_HANDLE gImg;
extern EFI_SYSTEM_TABLE* gSys;
extern EFI_BOOT_SERVICES* gBS;
extern SIMPLE_TEXT_OUTPUT_INTERFACE* gConOut;
