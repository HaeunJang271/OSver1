; gdt_flush.asm — reload segment registers after installing a new GDT

[bits 32]
[global gdt_flush]

gdt_flush:
    mov eax, [esp+4]    ; argument: pointer to gdt_ptr_t
    lgdt [eax]
    jmp 0x08:.reload_cs ; far jump reloads CS with kernel code selector
.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
