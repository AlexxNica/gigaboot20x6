THIS PROJECT IS NO LONGER MAINTAINED.
The gigaboot20x6 project is now hosted at
https://fuchsia.googlesource.com/magenta in the bootloader/ directory.



What is This?
-------------

This project contains some experiments in software that runs on UEFI
firmware for the purpose of exploring UEFI development and bootloader
development.

Since UEFI images are in PE32+ file format, we require that our binaries be
position independent executables with no relocations. For the most part this
does not require any extra effort on x86-64, but it does mean that you cannot
statically initialize any variables that hold an address. (These addresses may
be assigned at runtime however.)

External Software Included
--------------------------

Local Path:   third_party/ovmf/...
Description:  UEFI Firmware Suitable for use in Qemu
Distribution: http://www.tianocore.org/ovmf/
Version:      OVMF-X64-r15214.zip
License:      BSD-ish, see ovmf/LICENSE


External Dependencies
---------------------

qemu-system-x86_64 is needed to test in emulation
gnu parted and mtools are needed to generate the disk.img for Qemu


Useful Resources & Documentation
--------------------------------

ACPI & UEFI Specifications
http://www.uefi.org/specifications

Intel 64 and IA-32 Architecture Manuals
http://www.intel.com/content/www/us/en/processors/architectures-software-developer-manuals.html

Tianocore UEFI Open Source Community
(Source for OVMF, EDK II Dev Environment, etc)
http://www.tianocore.org/
https://github.com/tianocore

