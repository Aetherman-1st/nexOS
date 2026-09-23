[bits 32]
global _start
global _isr44
global _isr33
global _isr32
extern _main
extern _mouse_handler
extern _keyboard_handler
extern _timer_handler

_start:
    call _main
    jmp $

_isr44:
    pusha
    call _mouse_handler
    popa
    iretd

_isr33:
    pusha
    call _keyboard_handler
    popa
    iretd

_isr32:
    pusha
    call _timer_handler
    popa
    iretd
