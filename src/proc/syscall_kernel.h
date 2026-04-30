#pragma once
#include "../cpu/isr.h"

/* 커널 측 — INT 0x80 진입 시 isr.c 가 호출. */
void syscall_dispatch(registers_t *regs);
