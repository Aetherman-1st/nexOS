[bits 32]
global _start
global isr44
global isr33
extern _main
extern mouse_handler
extern keyboard_handler

_start:
    call _main
    jmp $

isr44:
    pusha
    call mouse_handler
    popa
    iretd

isr33:
    pusha
    call keyboard_handler
    popa
    iretd
