ENTRY(_start)
SECTIONS
{
    . = 0x8000;
    .text : { *(.text) *(.text.*) }
    .data : { *(.data) }
    .rdata : { *(.rdata) *(.rdata.*) }
    .bss  : { *(COMMON) *(.bss) }
    /DISCARD/ : {
        *(.idata) *(.idata.*)
        *(.reloc)
        *(.eh_frame) *(.eh_frame.*)
        *(.rsrc) *(.rsrc.*)
        *(.pdata) *(.xdata)
        *(.debug*) *(.comment) *(.note*)
    }
}
