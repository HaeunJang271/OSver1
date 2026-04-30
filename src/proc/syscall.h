#pragma once
#include "../include/types.h"
#include "../cpu/isr.h"

/* ── 시스템콜 번호 ─────────────────────────────────────────────────────── */
#define SYS_EXIT      0
#define SYS_WRITE     1
#define SYS_GETPID    2
#define SYS_SLEEP_MS  3

#define SYS_MAX       4

/* ── 커널측 디스패처 ──────────────────────────────────────────────────── */
/* isr.c 가 INT 0x80 진입 시 호출. regs->eax = num, ebx/ecx/edx = args.
   결과는 regs->eax 에 써넣어 ring 3 의 eax 로 돌려준다. */
void syscall_dispatch(registers_t *regs);

/* ── ring 3 inline wrappers ──────────────────────────────────────────────
   ring 3 user 함수 안에서 사용. cdecl 안 거치고 곧장 INT 0x80. */

static inline int32_t syscall0(uint32_t num) {
    int32_t r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(num) : "memory");
    return r;
}
static inline int32_t syscall1(uint32_t num, uint32_t a) {
    int32_t r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(num), "b"(a) : "memory");
    return r;
}
static inline int32_t syscall3(uint32_t num,
                               uint32_t a, uint32_t b, uint32_t c) {
    int32_t r;
    __asm__ volatile ("int $0x80"
                      : "=a"(r)
                      : "a"(num), "b"(a), "c"(b), "d"(c)
                      : "memory");
    return r;
}

/* user-side 편의 매크로 */
#define u_exit(code)              ((void)syscall1(SYS_EXIT,    (uint32_t)(code)))
#define u_write(fd, buf, len)     syscall3(SYS_WRITE, (uint32_t)(fd),    \
                                                       (uint32_t)(buf),   \
                                                       (uint32_t)(len))
#define u_getpid()                syscall0(SYS_GETPID)
#define u_sleep_ms(ms)            ((void)syscall1(SYS_SLEEP_MS, (uint32_t)(ms)))
