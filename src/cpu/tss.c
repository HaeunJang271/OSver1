#include "tss.h"
#include "gdt.h"

static tss_t kernel_tss;

void tss_init(void) {
    /* TSS 영역을 깨끗이 0으로 */
    uint8_t *p = (uint8_t *)&kernel_tss;
    for (uint32_t i = 0; i < sizeof(kernel_tss); i++) p[i] = 0;

    kernel_tss.ss0         = GDT_KDATA_SEL;
    kernel_tss.esp0        = 0;                       /* 첫 user task 만들 때 set */
    kernel_tss.iomap_base  = sizeof(kernel_tss);      /* IO bitmap 없음 */
    kernel_tss.cs          = GDT_KCODE_SEL;
    kernel_tss.ss          = kernel_tss.ds = kernel_tss.es =
                              kernel_tss.fs = kernel_tss.gs = GDT_KDATA_SEL;

    /* GDT slot 5 에 descriptor 박고 LTR 로 적재 */
    gdt_set_tss((uint32_t)&kernel_tss, sizeof(kernel_tss) - 1);

    uint16_t sel = GDT_TSS_SEL;
    __asm__ volatile ("ltr %0" : : "r"(sel));
}

void tss_set_esp0(uint32_t esp0) {
    kernel_tss.esp0 = esp0;
}
