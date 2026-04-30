#pragma once
#include "../include/types.h"

typedef struct {
    uint16_t limit_low;   /* Limit[0:15]  */
    uint16_t base_low;    /* Base[0:15]   */
    uint8_t  base_mid;    /* Base[16:23]  */
    uint8_t  access;      /* Access byte  */
    uint8_t  granularity; /* Flags + Limit[16:19] */
    uint8_t  base_high;   /* Base[24:31]  */
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

void gdt_init(void);
