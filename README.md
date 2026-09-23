# nexOS

**A 32-bit protected-mode kernel with paging, FAT32 filesystem, RTL8139 networking, HTTP browser, and Alpine Linux theme.**

---

## Summary

nexOS is a hobby operating system kernel written in C and x86 assembly.
It boots via BIOS, initializes a VESA framebuffer, and renders a graphical
desktop with windows, Paint, a terminal, and a browser. It includes
a FAT32 file system over ATA/IDE PIO, a 32-bit paging memory manager,
an RTL8139 Ethernet driver with TCP/IP networking, and an HTTP client.

This is a hobby project. It is not a production operating system.

---

## Architecture

| Component | Implementation |
|-----------|----------------|
| Boot | NASM MBR bootloader |
| Kernel | 32-bit protected-mode C + x86 assembly |
| Paging | Page directory + page table, CR0.PG enabled |
| Memory | Physical page bitmap allocator (kmalloc/kfree) |
| Graphics | VESA framebuffer, VMware VGA, 800x600x24bpp |
| Disk I/O | ATA/IDE PIO (ata.c) |
| File System | FAT32 BPB parsing, FAT traversal (fat32.c) |
| Network | RTL8139 driver, ARP, TCP, HTTP client (net.c) |
| Browser | Browser window with URL bar, navigation |
| Desktop | Window manager, Paint, terminal, taskbar |
| Input | PS/2 mouse and keyboard, IDT handlers |
| Theme | Alpine Linux dark theme (#1E1E2E, #89B4FA) |
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

- **START menu** (bottom-left): Terminal, Notepad, About, Browser, Shutdown
- **Paint**: Draw with the left mouse button
- **Terminal**: Type commands and press Enter
- **Browser**: HTTP browser with URL bar (click BROWSER in START menu)
- **Notepad**: Text input
- **Mouse**: PS/2 mouse cursor support
- **File Manager**: Browse ATA disk files via FAT32

---

## File Layout

```
boot.asm              # MBR bootloader (BIOS int 0x13)
kernel_entry.asm      # Protected-mode entry
kernel.c              # Desktop, terminal, paging, drivers, browser
ata.c / ata.h         # ATA/IDE PIO disk access
fat32.c / fat32.h     # FAT32 file system
net.c / net.h         # RTL8139, TCP/IP, HTTP client, DNS
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
