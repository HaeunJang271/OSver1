#include "syscall.h"
#include "task.h"
#include "../drivers/screen.h"
#include "../drivers/timer.h"

/* ── 개별 시스템콜 ─────────────────────────────────────────────────────── */

static void sys_exit_impl(int code) {
    (void)code;     /* 종료 코드는 일단 무시 — 향후 wait() 와 결합 */
    task_exit();    /* no return */
}

static int32_t sys_write_impl(int fd, const char *buf, uint32_t n) {
    if (fd != 1 && fd != 2) return -1;     /* stdout/stderr 만 지원 */
    if (!buf) return -1;
    /* 주의: ring 3 가 준 포인터 — 페이지가 안 매핑돼있으면 PF.
       이번 단계는 첫 4MB 식별 매핑을 user 가 모두 보므로 안전하다고 가정. */
    for (uint32_t i = 0; i < n; i++) kputchar(buf[i]);
    return (int32_t)n;
}

static int32_t sys_getpid_impl(void) {
    task_t *t = task_current();
    if (!t) return -1;
    return (int32_t)t->id;
}

static int32_t sys_sleep_ms_impl(uint32_t ms) {
    if (ms > 60000) ms = 60000;     /* user 가 너무 오래 잠들지 못하게 */
    timer_sleep_ms(ms);
    return 0;
}

/* ── 디스패처 ──────────────────────────────────────────────────────────── */

void syscall_dispatch(registers_t *regs) {
    uint32_t num = regs->eax;
    int32_t  ret = -1;

    switch (num) {
        case SYS_EXIT:
            sys_exit_impl((int)regs->ebx);
            /* sys_exit 는 절대 반환 안 함 (task_exit no-return).
               아래 코드는 언리치블 — 안전 fallback */
            ret = 0;
            break;

        case SYS_WRITE:
            ret = sys_write_impl((int)regs->ebx,
                                 (const char *)regs->ecx,
                                 regs->edx);
            break;

        case SYS_GETPID:
            ret = sys_getpid_impl();
            break;

        case SYS_SLEEP_MS:
            ret = sys_sleep_ms_impl(regs->ebx);
            break;

        default:
            ret = -1;
            break;
    }

    regs->eax = (uint32_t)ret;
}
