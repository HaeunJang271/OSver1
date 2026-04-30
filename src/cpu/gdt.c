#include "gdt.h"

#define GDT_ENTRIES 6      /* null + kcode + kdata + ucode + udata + tss */

static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;

extern void gdt_flush(uint32_t gdt_ptr_addr);

static void set_entry(int i, uint32_t base, uint32_t limit,
                      uint8_t access, uint8_t gran) {
    gdt[i].base_low   = base & 0xFFFF;
    gdt[i].base_mid   = (base >> 16) & 0xFF;
    gdt[i].base_high  = (base >> 24) & 0xFF;
    gdt[i].limit_low  = limit & 0xFFFF;
    gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].access     = access;
}

void gdt_set_tss(uint32_t base, uint32_t limit) {
    /* Available 32-bit TSS — access byte 0x89 (P=1, DPL=0, S=0, type=1001).
       TSS limit 은 byte granularity (G=0) 로 둔다 → flag nibble 0x0. */
    set_entry(5, base, limit, 0x89, 0x00);
}

void gdt_init(void) {
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint32_t)gdt;

    set_entry(0, 0, 0,          0x00, 0x00); /* Null descriptor              */
    set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* Kernel code (DPL 0)          */
    set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* Kernel data (DPL 0)          */
    set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); /* User code   (DPL 3)          */
    set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* User data   (DPL 3)          */
    /* slot 5 (TSS) 는 tss_init() 에서 gdt_set_tss() 로 채운 뒤 ltr 한다.   */

    gdt_flush((uint32_t)&gdt_ptr);
}
