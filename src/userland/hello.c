/* hello.c — Phase 12 데모 사용자 프로그램
 *
 * 별도 ELF 로 빌드되어 build/userland/hello.elf 가 된다.
 * MyOS 셸에서 `exec /HELLO.ELF` 로 실행. 모든 I/O 는 INT 0x80 syscall 만 사용. */

#include "../proc/syscall.h"

static uint32_t ulen(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void uitoa(int32_t v, char *out) {
    if (v < 0) { *out++ = '-'; v = -v; }
    char buf[16];
    int  i = 0;
    if (v == 0) { *out++ = '0'; *out = '\0'; return; }
    while (v > 0 && i < 15) { buf[i++] = (char)('0' + v % 10); v /= 10; }
    int j = 0;
    while (i > 0) out[j++] = buf[--i];
    out[j] = '\0';
}

static void uputs(const char *s) {
    u_write(1, s, ulen(s));
}

int main(void) {
    uputs("=== /HELLO.ELF (loaded from FAT32 disk!) ===\n");
    uputs("This program is a separate ELF binary.\n");
    uputs("It runs in ring 3 and talks to MyOS only via INT 0x80.\n");

    int32_t pid = u_getpid();
    char    pidbuf[16];
    uitoa(pid, pidbuf);
    uputs("[hello] my pid is ");
    uputs(pidbuf);
    uputs("\n");

    uputs("[hello] counting 1..3 with 1s sleep each:\n");
    for (int i = 1; i <= 3; i++) {
        char b[4]; b[0] = (char)('0' + i); b[1] = '\n'; b[2] = '\0';
        uputs("  > ");
        u_write(1, b, 2);
        u_sleep_ms(1000);
    }

    uputs("[hello] goodbye!\n");
    return 0;     /* crt0._start 가 SYS_EXIT 호출 */
}
