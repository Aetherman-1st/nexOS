[bits 32]
global _start
global _isr44
global _isr33
global _isr32
global _isr43
extern _main
extern _mouse_handler
extern _keyboard_handler
extern _timer_handler
extern _net_handler
extern _exception_handler

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

_isr43:
    pusha
    call _net_handler
    popa
    iretd

%macro ISR_NOERR 1
global _isr%1
_isr%1:
    push dword 0
    push dword %1
    jmp _isr_common
%endmacro

%macro ISR_ERR 1
global _isr%1
_isr%1:
    push dword %1
    jmp _isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR 29
ISR_ERR 30
ISR_NOERR 31

_isr_common:
    pusha
    push dword [esp + 36]
    push dword [esp + 36]
    call _exception_handler
    add esp, 8
    popa
    add esp, 8
    iret
