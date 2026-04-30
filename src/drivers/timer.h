#pragma once
#include "../include/types.h"

/* Programmable Interval Timer 드라이버.
   IRQ 0 으로 정확히 TIMER_HZ 횟수만큼 틱이 발생한다 (기본 100Hz, 10ms tick). */

#define TIMER_HZ 100

void     timer_init(void);
uint32_t timer_ticks(void);
uint32_t timer_uptime_seconds(void);
void     timer_sleep_ms(uint32_t ms);
