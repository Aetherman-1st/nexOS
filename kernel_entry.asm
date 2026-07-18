[bits 32]
global _start
global isr44
extern _main
extern mouse_handler

_start:
    call _main
    jmp $

isr44:
    pusha
    call mouse_handler
    popa
    iretd
