#include "idt.h"

#define IDT_ENTRIES 256

static idt_entry_t idt[IDT_ENTRIES]; /* zero-initialized → not-present by default */
static idt_ptr_t   idt_ptr;

extern void idt_flush(uint32_t idt_ptr_addr);
extern uint32_t isr_stub_table[]; /* defined in isr.asm */

void idt_set_gate(uint8_t num, uint32_t handler, uint16_t selector, uint8_t flags) {
    idt[num].offset_low  = handler & 0xFFFF;
    idt[num].offset_high = (handler >> 16) & 0xFFFF;
    idt[num].selector    = selector;
    idt[num].zero        = 0;
    idt[num].type_attr   = flags;
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)idt;

    /* Install all 32 exception stubs (vectors 0–31) */
    for (int i = 0; i < 32; i++)
        idt_set_gate((uint8_t)i, isr_stub_table[i], 0x08, 0x8E);

    /* Install 16 IRQ stubs (vectors 32–47, after PIC remap) */
    for (int i = 0; i < 16; i++)
        idt_set_gate((uint8_t)(32 + i), isr_stub_table[32 + i], 0x08, 0x8E);

    idt_flush((uint32_t)&idt_ptr);
}
