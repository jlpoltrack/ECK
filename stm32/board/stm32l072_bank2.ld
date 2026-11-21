/*
 * Linker script for STM32L072RB - Bank 2 (all HHKB models)
 * Flash: 64 KB starting at 0x08010000
 * RAM:   20 KB starting at 0x20000000
 */

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);

_Min_Heap_Size = 0;      /* no heap: FreeRTOS objects are static, nothing calls malloc */
_Min_Stack_Size = 0x400; /* 1 KB: static worst case ~720 B (AGENTS.md), measured peak 424 B */

MEMORY
{
    FLASH (rx)      : ORIGIN = 0x08010000, LENGTH = 64K
    RAM   (xrw)     : ORIGIN = 0x20000000, LENGTH = 20K
    EEPROM (rw)     : ORIGIN = 0x08080000, LENGTH = 6K
}

/* Separate load segments so none is both writable and executable (ld's RWX warning). */
PHDRS
{
    text    PT_LOAD FLAGS(5); /* R X: flash */
    ramfunc PT_LOAD FLAGS(5); /* R X: code copied to RAM at start-up */
    data    PT_LOAD FLAGS(6); /* R W */
    bss     PT_LOAD FLAGS(6); /* R W, RAM only (no flash image) */
}

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH :text

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)

        KEEP (*(.init))
        KEEP (*(.fini))

        . = ALIGN(4);
        _etext = .;
    } >FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } >FLASH

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP (*(.preinit_array*))
        PROVIDE_HIDDEN (__preinit_array_end = .);
    } >FLASH

    .init_array :
    {
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array*))
        PROVIDE_HIDDEN (__init_array_end = .);
    } >FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP (*(SORT(.fini_array.*)))
        KEEP (*(.fini_array*))
        PROVIDE_HIDDEN (__fini_array_end = .);
    } >FLASH

    /* .ramfunc and .data are contiguous in RAM and flash: start-up copies both in one pass. */
    _sidata = LOADADDR(.ramfunc);

    .ramfunc :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.RamFunc)
        *(.RamFunc*)
        . = ALIGN(4);
    } >RAM AT> FLASH :ramfunc

    .data :
    {
        . = ALIGN(4);
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH :data
    ASSERT(LOADADDR(.data) - LOADADDR(.ramfunc) == ADDR(.data) - ADDR(.ramfunc),
           ".ramfunc and .data must keep the same gap in flash and RAM (one-pass start-up copy)")

    . = ALIGN(4);
    .bss :
    {
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM AT> RAM :bss

    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE ( end = . );
        PROVIDE ( _end = . );
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >RAM

    /DISCARD/ :
    {
        libc.a ( * )
        libm.a ( * )
        libgcc.a ( * )
    }
}
