; idt_flush.asm — load the IDT register

[bits 32]
[global idt_flush]

idt_flush:
    mov eax, [esp+4]    ; argument: pointer to idt_ptr_t
    lidt [eax]
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
