#include "serial.h"
#include "../include/io.h"

/* ── COM1 포트와 레지스터 오프셋 ────────────────────────────────────────── */
#define COM1_BASE  0x3F8
#define COM1_DATA  (COM1_BASE + 0)   /* DLAB=0: TX/RX 데이터 */
#define COM1_IER   (COM1_BASE + 1)   /* DLAB=0: 인터럽트 활성 비트 */
#define COM1_DLL   (COM1_BASE + 0)   /* DLAB=1: divisor low  */
#define COM1_DLM   (COM1_BASE + 1)   /* DLAB=1: divisor high */
#define COM1_FCR   (COM1_BASE + 2)   /* FIFO control */
#define COM1_LCR   (COM1_BASE + 3)   /* line control (DLAB 비트 포함) */
#define COM1_MCR   (COM1_BASE + 4)   /* modem control */
#define COM1_LSR   (COM1_BASE + 5)   /* line status */

/* LSR 비트 5: Transmitter Holding Register Empty — 다음 바이트 송출 가능 */
#define LSR_THR_EMPTY 0x20

static int g_initialized = 0;

void serial_init(void) {
    /* 인터럽트 비활성 — 폴링 출력만 사용 */
    outb(COM1_IER, 0x00);

    /* DLAB on → 보레이트 divisor 설정.
       기본 클록 1843200 / 16 = 115200, divisor 1 → 115200 baud. */
    outb(COM1_LCR, 0x80);
    outb(COM1_DLL, 0x01);
    outb(COM1_DLM, 0x00);

    /* DLAB off, 8 bits / no parity / 1 stop */
    outb(COM1_LCR, 0x03);

    /* FIFO enable + clear RX/TX + 14바이트 트리거 */
    outb(COM1_FCR, 0xC7);

    /* DTR + RTS + OUT2(인터럽트 라인 게이트) */
    outb(COM1_MCR, 0x0B);

    g_initialized = 1;
}

int serial_is_ready(void) {
    return g_initialized;
}

static inline void wait_tx_empty(void) {
    while (!(inb(COM1_LSR) & LSR_THR_EMPTY)) ;
}

void serial_putc(char c) {
    if (!g_initialized) return;

    /* 호스트 터미널/로그 호환을 위해 LF → CRLF 변환 */
    if (c == '\n') {
        wait_tx_empty();
        outb(COM1_DATA, '\r');
    }
    wait_tx_empty();
    outb(COM1_DATA, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (*s) serial_putc(*s++);
}
