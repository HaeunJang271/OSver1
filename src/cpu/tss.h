#pragma once
#include "../include/types.h"

/* 32-bit Task State Segment (Intel SDM Vol.3 §7.2).
   Ring 3 → Ring 0 진입 시 (예: INT 0x80, IRQ) CPU 가 TSS 의 ss0/esp0 를
   읽어 자동으로 커널 스택으로 갈아끼운다. 우리는 단일 TSS 만 사용하고,
   매 컨텍스트 스위치마다 esp0 만 바꿔준다. */

typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;        /* ring 0 stack pointer ← 매 스위치마다 갱신 */
    uint32_t ss0;         /* ring 0 stack segment */
    uint32_t esp1, ss1;
    uint32_t esp2, ss2;
    uint32_t cr3;
    uint32_t eip, eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed)) tss_t;

/* GDT entry 5 에 TSS descriptor 를 셋업하고 ltr 로 적재한다. */
void tss_init(void);

/* Ring 0 으로 진입할 때 사용할 커널 스택 top 갱신 */
void tss_set_esp0(uint32_t esp0);
