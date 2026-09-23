[org 0x7c00]
KERNEL_OFFSET equ 0x8000
MODE_INFO     equ 0x0600
VBE_OUT       equ 0x5000
; +0x00 LFB addr     (4)
; +0x04 width        (2)
; +0x06 height       (2)
; +0x08 pitch        (2)
; +0x0A bpp          (1)
; +0x0B red_pos      (1)
; +0x0C green_pos    (1)
; +0x0D blue_pos     (1)

xor ax, ax
mov ds, ax
mov es, ax
mov fs, ax
mov gs, ax
mov ss, ax
mov sp, 0x7c00
mov [BOOT_DRIVE], dl
call load_kernel

; ── Try VESA modes in order (best first) ──
try_vesa:
mov si, vesa_modes
.next:
lodsw
cmp ax, 0
je vesa_fail
push si
mov cx, ax
mov [cur_mode], ax
mov ax, 0x4F01
mov di, MODE_INFO
int 0x10
cmp ax, 0x004F
jne .skip
mov ax, [MODE_INFO]
and ax, 0x0011
cmp ax, 0x0011
jne .skip
cmp dword [MODE_INFO + 0x28], 0
je .skip
; Set the mode, requesting the linear framebuffer.
mov ax, 0x4F02
pop si
mov bx, [cur_mode]
or bx, 0x4000
int 0x10
cmp ax, 0x004F
jne vesa_fail
; Re-query mode info: some BIOSes only fill in the LFB address after the
; mode is actually set, so read it back here and trust this copy.
mov ax, 0x4F01
mov cx, [cur_mode]
mov di, MODE_INFO
int 0x10
mov eax, [MODE_INFO + 0x28]
mov [VBE_OUT], eax
mov ax, [MODE_INFO + 0x12]
mov [VBE_OUT + 4], ax
mov ax, [MODE_INFO + 0x14]
mov [VBE_OUT + 6], ax
mov ax, [MODE_INFO + 0x10]
mov [VBE_OUT + 8], ax
mov al, [MODE_INFO + 0x19]
mov [VBE_OUT + 10], al
mov al, [MODE_INFO + 0x20]
mov [VBE_OUT + 11], al
mov al, [MODE_INFO + 0x22]
mov [VBE_OUT + 12], al
mov al, [MODE_INFO + 0x24]
mov [VBE_OUT + 13], al
jmp switch_to_pm
.skip:
pop si
jmp .next

vesa_fail:
mov ah, 0x00
mov al, 0x13
int 0x10
mov dword [VBE_OUT], 0xA0000
mov word [VBE_OUT+4], 320
mov word [VBE_OUT+6], 200
mov word [VBE_OUT+8], 320
mov byte [VBE_OUT+10], 8
mov byte [VBE_OUT+11], 16
mov byte [VBE_OUT+12], 8
mov byte [VBE_OUT+13], 0

switch_to_pm:
cli
lgdt [gdt_descriptor]
mov eax, cr0
or eax, 0x1
mov cr0, eax
jmp 0x08:init_pm

[bits 16]
load_kernel:
mov ah, 0x02
mov al, 40
mov ch, 0x00
mov dh, 0x00
mov cl, 0x02
mov dl, [BOOT_DRIVE]
mov bx, KERNEL_OFFSET
int 0x13
jc disk_error
ret

disk_error:
mov ax, 0x0b800
mov es, ax
mov word [es:0], 0x4f52
jmp $

[bits 32]
init_pm:
mov ax, 0x10
mov ds, ax
mov ss, ax
mov es, ax
mov fs, ax
mov gs, ax
mov ebp, 0x90000
mov esp, ebp
call KERNEL_OFFSET
jmp $

vesa_modes:
; Prefer 800x600 (24bpp then 16bpp), then 640x480.
dw 0x118, 0x11B, 0x115, 0x114, 0x112, 0x111, 0

gdt_start:
dq 0x0
gdt_code:
dw 0xffff, 0x0
db 0x0, 10011010b, 11001111b, 0x0
gdt_data:
dw 0xffff, 0x0
db 0x0, 10010010b, 11001111b, 0x0
gdt_end:
gdt_descriptor:
dw gdt_end - gdt_start - 1
dd gdt_start

BOOT_DRIVE db 0
cur_mode   dw 0

times 510-($-$$) db 0
dw 0xaa55
