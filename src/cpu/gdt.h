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

/* ── GDT 엔트리 인덱스 / selector ────────────────────────────────────────
   selector = (index << 3) | RPL */
#define GDT_KCODE_SEL  0x08          /* index 1, RPL 0 */
#define GDT_KDATA_SEL  0x10          /* index 2, RPL 0 */
#define GDT_UCODE_SEL  (0x18 | 3)    /* index 3, RPL 3 → 0x1B */
#define GDT_UDATA_SEL  (0x20 | 3)    /* index 4, RPL 3 → 0x23 */
#define GDT_TSS_SEL    0x28          /* index 5, RPL 0 */

void gdt_init(void);

/* tss.c 가 자기 base 를 GDT 엔트리 5 에 채울 수 있도록 노출 */
void gdt_set_tss(uint32_t base, uint32_t limit);
