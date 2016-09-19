# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ARCH		:= x86_64

EFI_TOOLCHAIN	:=
EFI_CC		:= $(EFI_TOOLCHAIN)gcc
EFI_LD		:= $(EFI_TOOLCHAIN)ld
EFI_OBJCOPY	:= $(EFI_TOOLCHAIN)objcopy
EFI_AR		:= $(EFI_TOOLCHAIN)ar

EFI_PATH	:= third_party/gnu-efi
EFI_LIB_PATHS	:= $(EFI_PATH)/$(ARCH)/lib $(EFI_PATH)/$(ARCH)/gnuefi out
EFI_INC_PATHS	:= $(EFI_PATH)/inc $(EFI_PATH)/inc/$(ARCH) $(EFI_PATH)/inc/protocol

EFI_CRT0	:= $(EFI_PATH)/$(ARCH)/gnuefi/crt0-efi-$(ARCH).o
EFI_LINKSCRIPT	:= $(EFI_PATH)/gnuefi/elf_$(ARCH)_efi.lds

EFI_CFLAGS	:= -fpic -fshort-wchar -fno-stack-protector -mno-red-zone
EFI_CFLAGS	+= -Wall
EFI_CFLAGS	+= -std=c99
EFI_CFLAGS	+= -ffreestanding -nostdinc -Iinclude -Isrc -Ithird_party/edk2 -Ithird_party/lk/include
EFI_CFLAGS	+= $(patsubst %,-I%,$(EFI_INC_PATHS))
EFI_CFLAGS	+= -DHAVE_USE_MS_ABI=1
EFI_CFLAGS	+= -ggdb

EFI_LDFLAGS	:= -nostdlib -znocombreloc -T $(EFI_LINKSCRIPT)
EFI_LDFLAGS	+= -shared -Bsymbolic
EFI_LDFLAGS	+= $(patsubst %,-L%,$(EFI_LIB_PATHS))

EFI_LIBS	:= -lutils -lefi -lgnuefi

what_to_build::	all

# build rules and macros
include build/build.mk

# declare applications here
#$(call efi_app, hello, hello.c)
$(call efi_app, showmem, src/showmem.c)
$(call efi_app, fileio, src/fileio.c)
OSBOOT_FILES := src/osboot.c \
				src/magenta.c \
				src/netboot.c \
				src/netifc.c \
				src/inet6.c

$(call efi_app, osboot, $(OSBOOT_FILES))
$(call efi_app, usbtest, src/usbtest.c)

ifneq ($(APP),)
	APP := out/$(APP).efi
else
	APP := out/osboot.efi
endif

LIB_SRCS := lib/utils.c lib/loadfile.c lib/console-printf.c lib/string.c
LIB_SRCS += third_party/lk/src/printf.c

LIB_OBJS := $(patsubst %.c,out/%.o,$(LIB_SRCS))
DEPS += $(patsubst %.c,out/%.d,$(LIB_SRCS))

out/libutils.a: $(LIB_OBJS)
	@mkdir -p $(dir $@)
	@echo archiving: $@
	$(QUIET)rm -f $@
	$(QUIET)ar rc $@ $^

out/BOOTx64.EFI: $(APP)
	@mkdir -p $(dir $@)
	$(QUIET)cp -f $^ $@

# generate a small IDE disk image for qemu
out/disk.img: $(APPS) out/BOOTx64.EFI
	@mkdir -p $(dir $@)
	$(QUIET)./build/mkdiskimg.sh $@
	@echo copying: $(APPS) README.txt to disk.img
	$(QUIET)mcopy -o -i out/disk.img@@1024K $(APPS) README.txt ::
	$(QUIET)mcopy -o -i out/disk.img@@1024K $(APPS) out/BOOTx64.EFI ::EFI/BOOT/

ALL += out/disk.img

-include $(DEPS)

# ensure gnu-efi gets built
$(EFI_CRT0):
	@echo building: gnu-efi
	$(QUIET)$(MAKE) -C $(EFI_PATH)

QEMU_OPTS := -cpu qemu64
QEMU_OPTS += -bios third_party/ovmf/OVMF.fd
QEMU_OPTS += -drive file=out/disk.img,format=raw,if=ide
QEMU_OPTS += -serial stdio
QEMU_OPTS += -m 256M
QEMU_OPTS += -usbdevice host:0b95:772b

qemu-e1000: QEMU_OPTS += -netdev type=tap,ifname=qemu,script=no,id=net0 -net nic,model=e1000,netdev=net0
qemu-e1000: all
	qemu-system-x86_64 $(QEMU_OPTS)

qemu: QEMU_OPTS += -net none
qemu:: all
	qemu-system-x86_64 $(QEMU_OPTS)

out/nbserver: src/nbserver.c
	@mkdir -p out
	@echo building nbserver
	$(QUIET)gcc -o out/nbserver -Isrc -Wall src/nbserver.c

all: $(ALL) out/nbserver

clean::
	rm -rf out

all-clean: clean
	make -C third_party/gnu-efi clean
