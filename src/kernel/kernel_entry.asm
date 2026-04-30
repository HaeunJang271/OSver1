; kernel_entry.asm — Kernel entry point
; This is the very first code that runs after the bootloader jumps to 0x10000.
; Calls kmain() defined in kernel.c.

[bits 32]
[global _start]
[extern kmain]

_start:
    call kmain

    ; kmain() must never return; halt the CPU if it does
    cli
.halt:
    hlt
    jmp .halt
