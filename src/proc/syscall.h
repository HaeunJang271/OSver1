#pragma once
#include "../include/types.h"

/* ── 시스템콜 번호 (user/kernel 양쪽 모두 사용) ───────────────────────── */
#define SYS_EXIT      0
#define SYS_WRITE     1
#define SYS_GETPID    2
#define SYS_SLEEP_MS  3

#define SYS_MAX       4

/* ── ring 3 inline wrappers ──────────────────────────────────────────────
   user-side 코드(예: src/userland/*.c, src/proc/user_demo.c)가 직접
   사용한다. cdecl 함수 호출 거치지 않고 INT 0x80 으로 직행. */

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
