ASM      := nasm
CC       := gcc
LD       := ld
OBJCOPY  := objcopy
QEMU     := qemu-system-i386

CFLAGS   := -m32 -ffreestanding -nostdlib -fno-pie -O2
LDFLAGS  := -m elf_i386 -N -e _start -T link.ld

all: nexos.img

boot.bin: boot.asm
	$(ASM) -f bin boot.asm -o $@

kernel_entry.o: kernel_entry.asm
	$(ASM) -f elf32 kernel_entry.asm -o $@

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.c -o $@

kernel.tmp: kernel_entry.o kernel.o link.ld
	$(LD) $(LDFLAGS) -o $@ kernel_entry.o kernel.o

kernel.bin: kernel.tmp
	$(OBJCOPY) -O binary kernel.tmp $@

nexos.img: boot.bin kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=64 2>/dev/null
	dd if=boot.bin    of=$@ bs=512 count=1  conv=notrunc 2>/dev/null
	dd if=kernel.bin  of=$@ bs=512 seek=1   conv=notrunc 2>/dev/null

run: nexos.img
	$(QEMU) -drive format=raw,file=nexos.img -vga std -m 64

clean:
	rm -f *.bin *.o *.tmp *.img

.PHONY: all run clean
