/*
 * STM32F429ZGT6 Linker Script
 *
 * Flash: 1MB physical @ 0x08000000, but linker-managed region is only the
 *        first 128KB. The remaining 896KB is reserved for application use
 *        (FLASH file storage, see App/Inc/app_flashstore.h).
 *
 *   [0x08000000, 0x08020000)  128KB  Sector 0-4    -> firmware (this script)
 *   [0x08020000, 0x08100000)  896KB  Sector 5-11   -> FLASH file storage
 *
 *   LENGTH below is intentionally shrunk to 128K so a growing firmware
 *   triggers a hard linker error ("region 'FLASH' overflowed") instead of
 *   silently overlapping the storage region.
 *
 * SRAM1: 112KB @ 0x20000000 (main RAM, DMA accessible)
 * CCM:   64KB  @ 0x10000000 (DMA NOT accessible)
 */

/* Entry point */
ENTRY(Reset_Handler)

/* Stack and heap sizes */
_Min_Stack_Size = 0x800;  /* 2 KB */
_Min_Heap_Size  = 0x000;  /* 0 - no malloc/free allowed */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
    SRAM1 (rwx) : ORIGIN = 0x20000000, LENGTH = 112K
    CCM   (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

SECTIONS
{
    /* Interrupt vector table */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    /* Program code */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)

        KEEP(*(.init))
        KEEP(*(.fini))

        . = ALIGN(4);
        _etext = .;
    } >FLASH

    /* Read-only data */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    /* ARM exception unwinding sections */
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

    /* Pre-init and init arrays */
    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } >FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } >FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } >FLASH

    /* Used by startup to initialize data */
    _sidata = LOADADDR(.data);

    /* Initialized data in SRAM1 */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >SRAM1 AT> FLASH

    /* Uninitialized data (zeroed) */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >SRAM1

    /* User heap and stack check */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >SRAM1

    /* Stack top = end of SRAM1 */
    _estack = ORIGIN(SRAM1) + LENGTH(SRAM1);

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
