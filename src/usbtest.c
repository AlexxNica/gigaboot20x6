// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi.h>
#include <efilib.h>

#include <utils.h>
#include <stdio.h>

#include <Protocol/UsbIo.h>

EFI_GUID UsbIoProtocol = EFI_USB_IO_PROTOCOL_GUID;

EFIAPI EFI_STATUS MyDriverSupported(
    EFI_DRIVER_BINDING* self, EFI_HANDLE ctlr,
    EFI_DEVICE_PATH* path) {

    EFI_USB_DEVICE_DESCRIPTOR dev;
    EFI_USB_IO_PROTOCOL* usbio;
    EFI_STATUS r;

    r = gBS->OpenProtocol(ctlr, &UsbIoProtocol,
                          (void**)&usbio, self->DriverBindingHandle,
                          ctlr, EFI_OPEN_PROTOCOL_BY_DRIVER);

    if (r == 0) {
        if (usbio->UsbGetDeviceDescriptor(usbio, &dev)) {
            return EFI_UNSUPPORTED;
        }
        printf("Supported? ctlr=%p vid=%04x pid=%04x\n",
               ctlr, dev.IdVendor, dev.IdProduct);
        gBS->CloseProtocol(ctlr, &UsbIoProtocol,
                           self->DriverBindingHandle, ctlr);
        return EFI_SUCCESS;
    }
    return EFI_UNSUPPORTED;
}

EFIAPI EFI_STATUS MyDriverStart(
    EFI_DRIVER_BINDING* self, EFI_HANDLE ctlr,
    EFI_DEVICE_PATH* path) {
    EFI_STATUS r;

    EFI_USB_IO_PROTOCOL* usbio;

    printf("Start! ctlr=%p\n", ctlr);

    r = gBS->OpenProtocol(ctlr, &UsbIoProtocol,
                          (void**)&usbio, self->DriverBindingHandle,
                          ctlr, EFI_OPEN_PROTOCOL_BY_DRIVER);

    // alloc device state, stash usbio with it
    // probably attached to a protocol installed on a child handle

    if (r) {
        printf("OpenProtocol Failed %lx\n", r);
        return EFI_DEVICE_ERROR;
    }
    return EFI_SUCCESS;
}

EFIAPI EFI_STATUS MyDriverStop(
    EFI_DRIVER_BINDING* self, EFI_HANDLE ctlr,
    UINTN count, EFI_HANDLE* children) {

    printf("Stop! ctlr=%p\n", ctlr);

    // recover device state, tear down

    gBS->CloseProtocol(ctlr, &UsbIoProtocol,
                       self->DriverBindingHandle, ctlr);
    return EFI_SUCCESS;
}

static EFI_DRIVER_BINDING MyDriver = {
    .Supported = MyDriverSupported,
    .Start = MyDriverStart,
    .Stop = MyDriverStop,
    .Version = 32,
    .ImageHandle = NULL,
    .DriverBindingHandle = NULL,
};

void InstallMyDriver(EFI_HANDLE img, EFI_SYSTEM_TABLE* sys) {
    EFI_BOOT_SERVICES* bs = sys->BootServices;
    EFI_HANDLE* list;
    UINTN count, i;
    EFI_STATUS r;

    MyDriver.ImageHandle = img;
    MyDriver.DriverBindingHandle = img;
    r = bs->InstallProtocolInterface(&img, &DriverBindingProtocol,
                                     EFI_NATIVE_INTERFACE, &MyDriver);
    if (r) {
        Print(L"DriverBinding failed %lx\n", r);
        return;
    }

    // For every Handle that supports UsbIoProtocol, try to connect the driver
    r = bs->LocateHandleBuffer(ByProtocol, &UsbIoProtocol, NULL, &count, &list);
    if (r == 0) {
        for (i = 0; i < count; i++) {
            r = bs->ConnectController(list[i], NULL, NULL, FALSE);
        }
        bs->FreePool(list);
    }
}

void RemoveMyDriver(EFI_HANDLE img, EFI_SYSTEM_TABLE* sys) {
    EFI_BOOT_SERVICES* bs = sys->BootServices;
    EFI_HANDLE* list;
    UINTN count, i;
    EFI_STATUS r;

    // Disconnect the driver
    r = bs->LocateHandleBuffer(ByProtocol, &UsbIoProtocol, NULL, &count, &list);
    if (r == 0) {
        for (i = 0; i < count; i++) {
            r = bs->DisconnectController(list[i], img, NULL);
        }
        bs->FreePool(list);
    }

    // Unregister so we can safely exit
    r = bs->UninstallProtocolInterface(img, &DriverBindingProtocol, &MyDriver);
    if (r)
        printf("UninstallProtocol failed %lx\n", r);
}

EFI_STATUS efi_main(EFI_HANDLE img, EFI_SYSTEM_TABLE* sys) {
    InitializeLib(img, sys);
    InitGoodies(img, sys);

    Print(L"Hello, EFI World\n");

    InstallMyDriver(img, sys);

    // do stuff

    RemoveMyDriver(img, sys);

    WaitAnyKey();
    return EFI_SUCCESS;
}
