#include "idt.h"

#define IDT_ENTRIES 256

static idt_entry_t idt[IDT_ENTRIES]; /* zero-initialized → not-present by default */
static idt_ptr_t   idt_ptr;

extern void idt_flush(uint32_t idt_ptr_addr);
extern uint32_t isr_stub_table[]; /* defined in isr.asm */
extern void isr128(void);          /* INT 0x80 syscall stub */

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

    /* INT 0x80 syscall — type 0xEF :
         P=1, DPL=11 (ring 3 가 호출 가능), S=0, type=1111 (32-bit trap gate).
         trap gate 는 진입 시 EFLAGS.IF 를 자동으로 끄지 않으므로 syscall
         도중에도 다른 IRQ 가 들어올 수 있다 (sleep 중 PIT 등). */
    idt_set_gate(128, (uint32_t)isr128, 0x08, 0xEF);

    idt_flush((uint32_t)&idt_ptr);
}
