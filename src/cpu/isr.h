#pragma once
#include "../include/types.h"

/* Stack frame built by isr_common before calling isr_handler().
   Members are ordered from lowest to highest address (i.e., last-pushed first). */
typedef struct {
    /* Segment registers (pushed last in isr_common) */
    uint32_t gs, fs, es, ds;
    /* General-purpose registers (pusha: EDI pushed last → lowest addr) */
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    /* Pushed by our ISR stub */
    uint32_t int_no, err_code;
    /* Pushed automatically by the CPU on interrupt */
    uint32_t eip, cs, eflags, useresp, ss;
} __attribute__((packed)) registers_t;

typedef void (*isr_handler_t)(registers_t *regs);

void isr_init(void);
void isr_register(uint8_t num, isr_handler_t handler);

/* Called from isr_common (assembly); routes to registered handler */
void isr_handler(registers_t *regs);
