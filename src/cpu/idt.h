#pragma once
#include "../include/types.h"

typedef struct {
    uint16_t offset_low;  /* Handler address[0:15]  */
    uint16_t selector;    /* Code segment selector   */
    uint8_t  zero;        /* Always 0                */
    uint8_t  type_attr;   /* 0x8E = present, ring 0, 32-bit interrupt gate */
    uint16_t offset_high; /* Handler address[16:31] */
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t handler, uint16_t selector, uint8_t flags);
