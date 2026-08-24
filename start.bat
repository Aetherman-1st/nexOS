@echo off
cd /d "%~dp0"
set PATH=C:\mingw64\bin;%PATH%

echo --- KILLING PROCESSES ---
taskkill /F /IM qemu-system-x86_64.exe /T >nul 2>&1

echo --- CLEANING ---
del *.bin *.o *.img *.tmp 2>nul

echo --- STEP 1: ASSEMBLY ---
nasm -f bin boot.asm -o boot.bin
nasm -f elf32 kernel_entry.asm -o kernel_entry.o

echo --- STEP 2: COMPILING C ---
gcc -ffreestanding -m32 -fno-asynchronous-unwind-tables -c kernel.c -o kernel.o

echo --- STEP 3: LINKING ---
ld -m i386pe -e _start -T link_pe.ld -o kernel.tmp kernel_entry.o kernel.o

echo --- STEP 4: STRIPPING HEADERS ---
objcopy -O binary kernel.tmp kernel.bin

echo --- STEP 5: STITCHING AND PADDING ---
copy /b boot.bin + kernel.bin nexos.img
fsutil file seteof nexos.img 32768

echo --- STEP 6: THE TRUTH CHECK ---
dir nexos.img
pause

echo --- STEP 7: RUNNING ( Made by Yazeed Omari)--- 
"C:\Program Files\qemu\qemu-system-x86_64.exe" -drive format=raw,file=nexos.img
pause
