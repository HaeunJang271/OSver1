#include "pic.h"
#include "../include/io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

void pic_init(void) {
    /* ICW1 — start initialization sequence */
    outb(PIC1_CMD,  0x11);  io_wait();
    outb(PIC2_CMD,  0x11);  io_wait();

    /* ICW2 — vector offsets:
       Master IRQ0-7  → INT 0x20-0x27 (vectors 32-39)
       Slave  IRQ8-15 → INT 0x28-0x2F (vectors 40-47) */
    outb(PIC1_DATA, 0x20);  io_wait();
    outb(PIC2_DATA, 0x28);  io_wait();

    /* ICW3 — cascade wiring */
    outb(PIC1_DATA, 0x04);  io_wait(); /* slave on IRQ2 */
    outb(PIC2_DATA, 0x02);  io_wait(); /* slave ID = 2  */

    /* ICW4 — 8086 mode */
    outb(PIC1_DATA, 0x01);  io_wait();
    outb(PIC2_DATA, 0x01);  io_wait();

    /* Unmask all IRQs */
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_mask_irq(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = irq < 8 ? irq : (uint8_t)(irq - 8);
    outb(port, (uint8_t)(inb(port) | (1u << bit)));
}

void pic_unmask_irq(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = irq < 8 ? irq : (uint8_t)(irq - 8);
    outb(port, (uint8_t)(inb(port) & ~(1u << bit)));
}
