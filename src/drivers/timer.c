#include "timer.h"
#include "../include/io.h"
#include "../cpu/isr.h"
#include "../proc/task.h"

/* PIT 8254 I/O 포트 */
#define PIT_CH0_DATA 0x40
#define PIT_COMMAND  0x43

/* PIT 입력 클록 (정확히는 1.193182 MHz) */
#define PIT_BASE_HZ  1193182u

/* IRQ 0 = 인터럽트 벡터 32 (PIC 마스터 오프셋 0x20) */
#define IRQ0_VECTOR  32

static volatile uint32_t g_ticks = 0;

static void timer_irq(registers_t *regs) {
    (void)regs;
    g_ticks++;
    /* 멀티태스킹이 활성화돼있으면 슬라이스 회계.
       실제 yield 는 isr.c 가 EOI 후에 한 번 처리. */
    task_tick();
}

void timer_init(void) {
    uint32_t divisor = PIT_BASE_HZ / TIMER_HZ;       /* 100Hz → 11932 */
    if (divisor == 0)        divisor = 1;
    if (divisor > 0xFFFFu)   divisor = 0xFFFFu;

    /* Command byte 0x36 :
         bits 7-6 (00) → counter 0
         bits 5-4 (11) → access lo+hi byte
         bits 3-1 (011) → mode 3 (square wave)
         bit  0   (0)  → 16-bit binary */
    outb(PIT_COMMAND,   0x36);
    outb(PIT_CH0_DATA, (uint8_t)( divisor       & 0xFF));
    outb(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    isr_register(IRQ0_VECTOR, timer_irq);
}

uint32_t timer_ticks(void) {
    return g_ticks;
}

uint32_t timer_uptime_seconds(void) {
    return g_ticks / TIMER_HZ;
}

void timer_sleep_ms(uint32_t ms) {
    /* TIMER_HZ=100 → 1 tick = 10ms. ms 값은 가장 가까운 tick 으로 반올림 */
    uint32_t ticks_to_wait = (ms + (1000u / TIMER_HZ) - 1) / (1000u / TIMER_HZ);
    uint32_t target = g_ticks + ticks_to_wait;
    while (g_ticks < target) {
        /* hlt 로 인터럽트 대기 — 다음 PIT 틱이 깨워준다 (CPU 안 굴림) */
        __asm__ volatile ("hlt");
    }
}
