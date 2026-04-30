/*
 * Linker script for MyOS kernel
 * Kernel is loaded to physical address 0x10000 by the bootloader.
 */

ENTRY(_start)

SECTIONS {
    . = 0x10000;

    .text ALIGN(4) : {
        *(.text)
        *(.text.*)
    }

    .rodata ALIGN(4) : {
        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(4) : {
        *(.data)
    }

    .bss ALIGN(4096) : {
        bss_start = .;      /* kernel_entry.asm uses these to zero BSS */
        *(COMMON)
        *(.bss)
        . = ALIGN(4);
        bss_end = .;
    }
}
