#pragma once
#include "../include/types.h"

void pic_init(void);          /* Remap PIC and unmask all IRQs */
void pic_send_eoi(uint8_t irq);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
