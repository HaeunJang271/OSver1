#include "syscall.h"

/* ── Phase 11 데모 — ring 3 에서 실행되는 함수 ───────────────────────────
   여기 코드는 커널 binary 의 일부지만, paging.c 가 첫 4MB 를 USER 비트와
   함께 매핑했기 때문에 ring 3 에서 fetch/실행이 가능하다.

   ring 3 코드는 cli/sti/inb/outb/hlt 등 privileged instruction 을 부르면
   GP fault 가 난다. 화면 출력도 직접 VGA 메모리를 건드리지 않고 반드시
   syscall 을 통해 커널에 위임해야 한다. */

/* user 측에서는 strlen 같은 libc 가 없으므로 직접 작성 */
static uint32_t ulen(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

/* uint32_t → 십진 문자열 (역순 → 뒤집기) */
static void u_itoa(uint32_t v, char *out) {
    char buf[16];
    int  i = 0;
    if (v == 0) { out[0] = '0'; out[1] = '\0'; return; }
    while (v > 0 && i < 15) { buf[i++] = (char)('0' + v % 10); v /= 10; }
    int j = 0;
    while (i > 0) out[j++] = buf[--i];
    out[j] = '\0';
}

void user_demo_entry(void) {
    const char *hello = "[ring 3] Hello from user mode!\n";
    u_write(1, hello, ulen(hello));

    int32_t pid = u_getpid();
    char    pidbuf[16];
    u_itoa((uint32_t)pid, pidbuf);

    const char *p1 = "[ring 3] my pid = ";
    u_write(1, p1, ulen(p1));
    u_write(1, pidbuf, ulen(pidbuf));
    u_write(1, "\n", 1);

    const char *zz = "[ring 3] sleeping 2 seconds...\n";
    u_write(1, zz, ulen(zz));
    u_sleep_ms(2000);

    const char *bye = "[ring 3] bye!\n";
    u_write(1, bye, ulen(bye));

    u_exit(0);

    /* 도달 불가 — u_exit 가 SYS_EXIT 호출, kernel 의 task_exit() 가
       절대 ring 3 로 돌아오지 않는다. fall through 시 GP fault 가 안전망. */
    for (;;) { }
}
