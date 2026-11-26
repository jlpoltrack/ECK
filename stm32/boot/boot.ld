/* ECK boot stub: STM32L072 Bank 1 (0x08000000), under 3 KB (tools check 0xFF from 0xC00; Bluetooth
 * updates stage from 0x1000). No RAM data; one RAM function (Bank 2 half-page programming). */
ENTRY(Reset_Handler)
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 3K
    /* Above ST's ROM loader data (to 0x200014D0), below the stack (0x20004000). */
    RAMCODE (rwx) : ORIGIN = 0x20002000, LENGTH = 256
}
PHDRS
{
    text PT_LOAD FLAGS(5);
    ramcode PT_LOAD FLAGS(5);
}
SECTIONS
{
    .text :
    {
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4); /* .ramcode's load address: the reset copy reads words (unaligned faults) */
    } >FLASH :text
    .ramcode :
    {
        _ramcode_start = .;
        *(.ramcode*)
        . = ALIGN(4); /* start-up copies whole words */
        _ramcode_end = .;
    } >RAMCODE AT> FLASH :ramcode
    _ramcode_load = LOADADDR(.ramcode);
    ASSERT((_ramcode_load & 3) == 0 && (ADDR(.ramcode) & 3) == 0, ".ramcode must be word-aligned in flash and RAM")
    .data : { *(.data*) *(.bss*) *(COMMON) }
    ASSERT(SIZEOF(.data) == 0, "the boot stub must not use RAM data")
    /DISCARD/ : { *(.ARM.*) *(.comment) }
}
