#include "isr.h"
#include "pic.h"
#include "../drivers/screen.h"

static isr_handler_t handlers[256];

static const char *exception_names[21] = {
    "Division Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD FP Exception",
    "Virtualization Exception",
};

void isr_register(uint8_t num, isr_handler_t handler) {
    handlers[num] = handler;
}

/* Called from isr_common in isr.asm */
void isr_handler(registers_t *regs) {
    if (regs->int_no < 32) {
        /* CPU exception */
        if (handlers[regs->int_no]) {
            handlers[regs->int_no](regs);
            return;
        }
        kprint_color("\n*** EXCEPTION ***  ", VGA_WHITE, VGA_RED);
        if (regs->int_no < 21)
            kprint_color(exception_names[regs->int_no], VGA_WHITE, VGA_RED);
        kprint("\n");
        kprint_color("INT=", VGA_LIGHT_GREY, VGA_BLACK); kprint_dec(regs->int_no);
        kprint_color("  ERR=", VGA_LIGHT_GREY, VGA_BLACK); kprint_hex(regs->err_code);
        kprint_color("  EIP=", VGA_LIGHT_GREY, VGA_BLACK); kprint_hex(regs->eip);
        kprint("\nSystem halted.\n");
        for (;;) __asm__ volatile ("cli; hlt");
    } else if (regs->int_no < 48) {
        /* Hardware IRQ */
        uint8_t irq = (uint8_t)(regs->int_no - 32);
        if (handlers[regs->int_no])
            handlers[regs->int_no](regs);
        pic_send_eoi(irq);
    }
}

void isr_init(void) {
    /* Handlers are registered individually via isr_register() */
}
