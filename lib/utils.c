// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <efi.h>
#include <efilib.h>

#include <utils.h>
#include <printf.h>

#define ERR_ENTRY(x) { #x, L"" #x }
typedef struct {
    const char *str;
    const CHAR16 *w_str;
} err_entry_t;

err_entry_t efi_error_labels[] = {
    ERR_ENTRY(EFI_SUCCESS),
    ERR_ENTRY(EFI_LOAD_ERROR),
    ERR_ENTRY(EFI_INVALID_PARAMETER),
    ERR_ENTRY(EFI_UNSUPPORTED),
    ERR_ENTRY(EFI_BAD_BUFFER_SIZE),
    ERR_ENTRY(EFI_BUFFER_TOO_SMALL),
    ERR_ENTRY(EFI_NOT_READY),
    ERR_ENTRY(EFI_DEVICE_ERROR),
    ERR_ENTRY(EFI_WRITE_PROTECTED),
    ERR_ENTRY(EFI_OUT_OF_RESOURCES),
    ERR_ENTRY(EFI_VOLUME_CORRUPTED),
    ERR_ENTRY(EFI_VOLUME_FULL),
    ERR_ENTRY(EFI_NO_MEDIA),
    ERR_ENTRY(EFI_MEDIA_CHANGED),
    ERR_ENTRY(EFI_NOT_FOUND),
    ERR_ENTRY(EFI_ACCESS_DENIED),
    ERR_ENTRY(EFI_NO_RESPONSE),
    ERR_ENTRY(EFI_NO_MAPPING),
    ERR_ENTRY(EFI_TIMEOUT),
    ERR_ENTRY(EFI_NOT_STARTED),
    ERR_ENTRY(EFI_ALREADY_STARTED),
    ERR_ENTRY(EFI_ABORTED),
    ERR_ENTRY(EFI_ICMP_ERROR),
    ERR_ENTRY(EFI_TFTP_ERROR),
    ERR_ENTRY(EFI_PROTOCOL_ERROR),
    ERR_ENTRY(EFI_INCOMPATIBLE_VERSION),
    ERR_ENTRY(EFI_SECURITY_VIOLATION),
    ERR_ENTRY(EFI_CRC_ERROR),
    ERR_ENTRY(EFI_END_OF_MEDIA),
    ERR_ENTRY(EFI_END_OF_FILE),
    ERR_ENTRY(EFI_INVALID_LANGUAGE),
    ERR_ENTRY(EFI_COMPROMISED_DATA),
};

// Useful GUID Constants Not Defined by -lefi
EFI_GUID SimpleFileSystemProtocol = SIMPLE_FILE_SYSTEM_PROTOCOL;
EFI_GUID FileInfoGUID = EFI_FILE_INFO_ID;

// -lefi has its own globals, but this may end up not
// depending on that, so let's not depend on those
EFI_SYSTEM_TABLE* gSys;
EFI_HANDLE gImg;
EFI_BOOT_SERVICES* gBS;
SIMPLE_TEXT_OUTPUT_INTERFACE* gConOut;

void InitGoodies(EFI_HANDLE img, EFI_SYSTEM_TABLE* sys) {
    gSys = sys;
    gImg = img;
    gBS = sys->BootServices;
    gConOut = sys->ConOut;
}

void WaitAnyKey(void) {
    SIMPLE_INPUT_INTERFACE* sii = gSys->ConIn;
    EFI_INPUT_KEY key;
    while (sii->ReadKeyStroke(sii, &key) != EFI_SUCCESS)
        ;
}

void Fatal(const char* msg, EFI_STATUS status) {
    printf("\nERROR: %s (%s)\n", msg, efi_strerror(status));
    WaitAnyKey();
    gBS->Exit(gImg, 1, 0, NULL);
}

CHAR16* HandleToString(EFI_HANDLE h) {
    EFI_DEVICE_PATH* path = DevicePathFromHandle(h);
    if (path == NULL)
        return L"<NoPath>";
    CHAR16* str = DevicePathToStr(path);
    if (str == NULL)
        return L"<NoString>";
    return str;
}

EFI_STATUS OpenProtocol(EFI_HANDLE h, EFI_GUID* guid, void** ifc) {
    return gBS->OpenProtocol(h, guid, ifc, gImg, NULL,
                             EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
}

EFI_STATUS CloseProtocol(EFI_HANDLE h, EFI_GUID* guid) {
    return gBS->CloseProtocol(h, guid, gImg, NULL);
}

const char *efi_strerror(EFI_STATUS status)
{
    size_t i = (~EFI_ERROR_MASK & status);
    if (i < sizeof(efi_error_labels)/sizeof(efi_error_labels[0])) {
        return efi_error_labels[i].str;
    }

    return "<Unknown error>";
}

const CHAR16 *efi_wstrerror(EFI_STATUS status)
{
    size_t i = (~EFI_ERROR_MASK & status);
    if (i < sizeof(efi_error_labels)/sizeof(efi_error_labels[0])) {
        return efi_error_labels[i].w_str;
    }

    return L"<Unknown error>";
}

size_t strlen_16(CHAR16 *str)
{
    size_t len = 0;
    while (*(str + len) != '\0') {
        len++;
    }

    return len;
}
