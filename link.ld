SECTIONS
{
    . = 0x8000;
    .text : { kernel_entry.o(.text) *(.text) *(.text.startup) }
    .rodata : { *(.rodata) *(.rodata.*) }
    .data : { *(.data) }
    .bss : { *(.bss) }
}
