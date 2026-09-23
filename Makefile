ASM      := nasm
CC       := gcc
LD       := ld
OBJCOPY  := objcopy
QEMU     := qemu-system-i386

CFLAGS   := -m32 -ffreestanding -nostdlib -fno-pie -O2 -fleading-underscore
LDFLAGS  := -m elf_i386 -N -e _start -T link.ld

all: nexos.img

boot.bin: boot.asm
	$(ASM) -f bin boot.asm -o $@

kernel_entry.o: kernel_entry.asm
	$(ASM) -f elf32 kernel_entry.asm -o $@

ata.o: ata.c ata.h io.h
	$(CC) $(CFLAGS) -c ata.c -o $@

fat32.o: fat32.c fat32.h ata.h io.h
	$(CC) $(CFLAGS) -c fat32.c -o $@

pci.o: pci.c pci.h io.h
	$(CC) $(CFLAGS) -c pci.c -o $@

acpi.o: acpi.c acpi.h io.h
	$(CC) $(CFLAGS) -c acpi.c -o $@

usb.o: usb.c usb.h io.h pci.h
	$(CC) $(CFLAGS) -c usb.c -o $@

net.o: net.c net.h io.h ata.h pci.h
	$(CC) $(CFLAGS) -c net.c -o $@

kernel.o: kernel.c io.h ata.h fat32.h net.h pci.h acpi.h usb.h
	$(CC) $(CFLAGS) -c kernel.c -o $@

kernel.tmp: kernel_entry.o ata.o fat32.o pci.o acpi.o usb.o net.o kernel.o link.ld
	$(LD) $(LDFLAGS) -o $@ kernel_entry.o ata.o fat32.o pci.o acpi.o usb.o net.o kernel.o
	@test "$$(nm $@ | awk '/ _start$$/{print $$1}')" = "00008000" || (echo "FATAL: _start not at 0x8000"; exit 1)

kernel.bin: kernel.tmp
	$(OBJCOPY) -O binary kernel.tmp $@

nexos.img: boot.bin kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=512 2>/dev/null
	dd if=boot.bin    of=$@ bs=512 count=1 conv=notrunc 2>/dev/null
	dd if=kernel.bin  of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null

run: nexos.img
	$(QEMU) -drive format=raw,file=nexos.img -device rtl8139,netdev=n0 -netdev user,id=n0 -vga vmware -m 256

clean:
	rm -f *.bin *.o *.tmp *.img

.PHONY: all run clean