#pragma once
#include "types.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* 디스크 PIO 등에서 한 번에 N개의 16비트 워드를 읽을 때 사용 */
static inline void insw(uint16_t port, void *buf, uint32_t count) {
    __asm__ volatile ("rep insw"
                      : "+D"(buf), "+c"(count)
                      : "d"(port)
                      : "memory");
}

/* 디스크 PIO write: N 워드를 한 번에 쓰기 */
static inline void outsw(uint16_t port, const void *buf, uint32_t count) {
    __asm__ volatile ("rep outsw"
                      : "+S"(buf), "+c"(count)
                      : "d"(port));
}

/* ~1 µs delay via unused port — used when initializing hardware */
static inline void io_wait(void) {
    outb(0x80, 0);
}
