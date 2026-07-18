[bits 16]
[org 0x8000]

    
    mov ax, 0x0013
    int 0x10

    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp 0x08:init_32bit

[bits 32]
init_32bit:
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    
    mov edi, 0xA0000        
    mov ecx, 320 * 200      
    mov al, 1               
    rep stosb               

    
    mov edi, 0xA0000 + (320 * 180)
    mov ecx, 320 * 20       
    mov al, 7               
    rep stosb

    
    mov edi, 0xA0000 + (320 * 50) + 60
    mov edx, 80             ; Height
.win_height:
    mov ecx, 200            ; Width
.win_width:
    mov byte [edi], 15      ; White
    inc edi
    loop .win_width
    add edi, 320 - 200      
    dec edx
    jnz .win_height

    
    mov edi, 0xA0000 + (320 * 50) + 60
    mov edx, 10            
.title_height:
    mov ecx, 200
.title_width:
    mov byte [edi], 9       
    inc edi
    loop .title_width
    add edi, 320 - 200
    dec edx
    jnz .title_height


    mov al, 0xA8        
    out 0x64, al
    
    mov al, 0x20      
    out 0x64, al
    
    ; Wait for data
    in al, 0x60
    or al, 2            
    push eax
    
    mov al, 0x60        
    out 0x64, al
    pop eax
    out 0x60, al        
    
    ; Tell the mouse itself to start sending data
    mov al, 0xD4
    out 0x64, al
    mov al, 0xF4      
    out 0x60, al
    jmp $



align 4
gdt_start:
    dq 0x0
gdt_code:
    dw 0xffff, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xffff, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start