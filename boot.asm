[org 0x7c00]
KERNEL_OFFSET equ 0x8000

mov [BOOT_DRIVE], dl

xor ax, ax
mov ds, ax
mov es, ax
mov fs, ax
mov gs, ax

mov bp, 0x7c00
mov sp, bp

call load_kernel

; ==========================================================
; GRAPHICS SWITCH: Force BIOS into VGA Mode 13h (320x200)
; ==========================================================
mov ah, 0x00        
mov al, 0x13        
int 0x10            

call switch_to_pm
jmp $

[bits 16]
load_kernel:
    mov ah, 0x02
    mov al, 40          ; Load exactly 40 sectors
    mov ch, 0x00        ; Cylinder 0
    mov dh, 0x00        ; Head 0
    mov cl, 0x02        ; Start at Sector 2 (Kernel location)
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

switch_to_pm:
    cli 
    lgdt [gdt_descriptor] 
    mov eax, cr0
    or eax, 0x1 
    mov cr0, eax
    jmp 0x08:init_pm 

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

BOOT_DRIVE db 0

times 510-($-$$) db 0
dw 0xaa55
