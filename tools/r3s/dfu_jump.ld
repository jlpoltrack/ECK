/* Temporary R3S Bank 2 image: vectors fill page 0, code starts at page 1. No RAM data. */
ENTRY(Reset_Handler)
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08010000, LENGTH = 64K
}
PHDRS
{
    text PT_LOAD FLAGS(5);
}
SECTIONS
{
    .text :
    {
        KEEP(*(.isr_vector))
        . = 0x80;
        *(.text*)
        *(.rodata*)
    } >FLASH :text
    .data : { *(.data*) *(.bss*) *(COMMON) }
    ASSERT(SIZEOF(.data) == 0, "the image must not use RAM data")
    /DISCARD/ : { *(.ARM.*) *(.comment) }
}
