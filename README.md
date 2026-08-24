# nexOS

a totally real operating system.

written by [Yazeed Omari](https://github.com/Aetherman-1st), one bug at a time.

---

## What is this

A hobby OS that boots off a raw disk image, switches to protected mode, sets up a
VESA framebuffer, and then draws you a whole fake desktop with a mouse cursor,
windows, Paint, a terminal, and a taskbar that says "START". None of it touches a
real filesystem, which is either a design decision or an accident. You decide.

Is it useful? No.
Is it safe? It runs in QEMU, so the worst it can do is disappoint you.
Does it have a `SUDO` command? Yes. Does it do what you think? No.

## Requirements

Oh, so you're actually going to run this. Bold. Here's what you need:

- [NASM](https://www.nasm.us/) — because writing opcodes by hand builds character
- [MinGW-w64](https://www.mingw-w64.org/) (Windows) or a normal `gcc` + `binutils` (Linux) — 32-bit support required
- [QEMU](https://www.qemu.org/) — the emulator that will heroically pretend this is a real OS
- Either the patience to read the build script, or the faith to just double-click it

## How to launch

### Windows (the easy way, if you can call it that)

1. Install NASM and make sure `nasm` is on your PATH.
2. Install MinGW-w64 and make sure `gcc`, `ld`, and `objcopy` are on your PATH.
3. Install QEMU, or point `start.bat` at wherever `qemu-system-x86_64.exe` lives.
4. Double-click `start.bat`, or run it from a terminal.
5. Stare at the window. That's it. That's the OS. Congratulations.

`start.bat` assembles the bootloader, compiles the kernel, stitches them into
`nexos.img`, and fires up QEMU for you. It even kills any lingering QEMU
process first, because last time didn't end well.

### Linux / WSL (for people who type instead of click)

```sh
make          # builds nexos.img
make run      # boots it in QEMU
```

Or by hand, because you clearly enjoy the scenic route:

```sh
nasm -f bin boot.asm -o boot.bin
nasm -f elf32 kernel_entry.asm -o kernel_entry.o
gcc -m32 -ffreestanding -nostdlib -fno-pie -O2 -c kernel.c -o kernel.o
ld -m elf_i386 -N -e _start -T link.ld -o kernel.tmp kernel_entry.o kernel.o
objcopy -O binary kernel.tmp kernel.bin
dd if=/dev/zero of=nexos.img bs=512 count=64
dd if=boot.bin   of=nexos.img bs=512 count=1 conv=notrunc
dd if=kernel.bin of=nexos.img bs=512 seek=1  conv=notrunc
qemu-system-i386 -drive format=raw,file=nexos.img
```

If any of that feels like work, just remember: someone wrote this entire OS so
you could have a taskbar with three buttons.

## What you can actually do in it

- **Mouse**: works. Probably. QEMU may need you to click inside the window first.
- **START** menu (bottom-left): Terminal, Notepad, About, and a red SHUTDOWN.
- **Paint**: draw with the left mouse button. It's the closest thing this OS has to a killer app.
- **Terminal**: type commands and press Enter. Try `HELP`, `ABOUT`, `CLEAR`, `CREDITS`, or `SUDO`.
- **File Manager**: lists files that don't exist. Peak accuracy.

## Terminal secrets

The terminal understands more than it lets on. Try `HELP`, `ABOUT`, `CLEAR`,
`ECHO`, `CREDITS`, `YAZEED`, `OMARI`, `SUDO`, `NEXOS`, `HELLO`, `SECRET`, and
`GOD`. Most of them just talk back. Some of them have a personality.

## Disclaimer

This is a toy, not a production OS. Do not put it on your resume. Do not replace
your daily driver. Do not ask it to read your files, because it genuinely cannot.
If it crashes, it was going to anyway. If it works, take the win and go outside.

## License

MIT, probably. Treat the code however you like. The author retains the right to
pretend none of it ever happened.
