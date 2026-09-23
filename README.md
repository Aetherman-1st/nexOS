# nexOS

**A 32-bit protected-mode kernel with FAT32 filesystem, ATA disk driver, VESA framebuffer desktop, terminal, and Alpine Linux theme.**

---

## Summary

nexOS is a hobby operating system kernel written in C and x86 assembly.
It boots via BIOS or GRUB, initializes a VESA framebuffer, and renders
a graphical desktop with windows, Paint, and a terminal. It includes
a FAT32 file system over ATA/IDE PIO, a physical memory allocator
with paging, and an Alpine Linux dark theme.

This is a hobby project. It is not a production operating system.

---

## Architecture

| Component | Implementation |
|-----------|----------------|
| Boot | NASM MBR bootloader + protected mode switch |
| Kernel | 32-bit protected-mode C + x86 assembly |
| Paging | Page directory + page table, CR0.PG enabled |
| Memory | Physical page bitmap allocator (kmalloc/kfree) |
| Graphics | VESA framebuffer, VMware VGA, 800x600x24bpp |
| Disk I/O | ATA/IDE PIO (ata.c) |
| File System | FAT32 BPB parsing, FAT traversal (fat32.c) |
| Desktop | Window manager, Paint, terminal, taskbar |
| Input | PS/2 mouse and keyboard, IDT handlers |
| Build | GCC `-m32 -ffreestanding`, NASM, `ld`, `objcopy` |

---

## Requirements

- NASM — bootloader assembly
- GCC with `-m32` support — kernel compilation
- binutils — linking (`ld`, `objcopy`)
- QEMU — emulation and testing

---

## Build

```sh
make              # builds nexos.img
make run          # boots nexos.img in QEMU
make clean        # removes build artifacts
```

Manual build:
```sh
nasm -f bin boot.asm -o boot.bin
nasm -f elf32 kernel_entry.asm -o kernel_entry.o
gcc -m32 -ffreestanding -nostdlib -fno-pie -O2 -fleading-underscore -c ata.c -o ata.o
gcc -m32 -ffreestanding -nostdlib -fno-pie -O2 -fleading-underscore -c fat32.c -o fat32.o
gcc -m32 -ffreestanding -nostdlib -fno-pie -O2 -fleading-underscore -c kernel.c -o kernel.o
ld -m elf_i386 -N -e _start -T link.ld -o kernel.tmp kernel_entry.o ata.o fat32.o kernel.o
objcopy -O binary kernel.tmp kernel.bin
dd if=/dev/zero of=nexos.img bs=512 count=64
dd if=boot.bin of=nexos.img bs=512 count=1 conv=notrunc
dd if=kernel.bin of=nexos.img bs=512 seek=1 conv=notrunc
qemu-system-i386 -drive format=raw,file=nexos.img -vga vmware -m 256
```

---

## Terminal Commands

| Command | Description |
|---------|-------------|
| `HELP` | Display available commands |
| `ABOUT` | About nexOS |
| `CLEAR` | Clear terminal |
| `ECHO` | Echo text |
| `CREDITS` | Project credits |
| `SUDO` | Sudo command |
| `LS` | List files (ATA disk) |
| `CAT` | Display file contents |
| `PING` | Send ARP request |
| `FETCH` | Execute HTTP GET request |
| `YAZEED` | Display developer info |
| `OMARI` | Display developer info |
| `NEXOS` | Display nexOS info |
| `HELLO` | Greeting |
| `SECRET` | Secret command |
| `GOD` | God command |

---

## Desktop Features

- **START menu** (bottom-left): Terminal, Paint, Notepad, About, Shutdown
- **Paint**: Draw with the left mouse button
- **Terminal**: Type commands and press Enter
- **Notepad**: Text input
- **Mouse**: PS/2 mouse cursor support
- **File Manager**: Browse ATA disk files via FAT32

---

## File Layout

```
boot.asm              # MBR bootloader (BIOS int 0x13)
kernel_entry.asm      # Protected-mode entry
kernel.c              # Desktop, terminal, I/O, paging, drivers
ata.c / ata.h         # ATA/IDE PIO disk access
fat32.c / fat32.h     # FAT32 file system
io.h                  # I/O primitives + string functions
link.ld               # ELF linker script
Makefile              # Build system
nexos.img             # Bootable disk image
```

---

## Disclaimer

This is a hobby project. Not a production OS. It does not replace
any existing operating system. It is a learning project.

---

## License

MIT, probably.
