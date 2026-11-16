/* ECK boot stub: STM32L072 Bank 1 (0x08000000), code only, no RAM data. */
ENTRY(Reset_Handler)
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 4K
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
        *(.text*)
        *(.rodata*)
    } >FLASH :text
    .data : { *(.data*) *(.bss*) *(COMMON) }
    ASSERT(SIZEOF(.data) == 0, "the boot stub must not use RAM data")
    /DISCARD/ : { *(.ARM.*) *(.comment) }
}
