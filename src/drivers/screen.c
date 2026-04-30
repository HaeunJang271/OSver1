#include "screen.h"

#define VGA_BASE   ((volatile unsigned char *)0xB8000)
#define COLS       80
#define ROWS       25

static int cur_col = 0;
static int cur_row = 0;

static void scroll_up(void) {
    unsigned char blank = vga_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int row = 0; row < ROWS - 1; row++) {
        for (int col = 0; col < COLS; col++) {
            int src = ((row + 1) * COLS + col) * 2;
            int dst = (row * COLS + col) * 2;
            VGA_BASE[dst]     = VGA_BASE[src];
            VGA_BASE[dst + 1] = VGA_BASE[src + 1];
        }
    }
    for (int col = 0; col < COLS; col++) {
        int pos = ((ROWS - 1) * COLS + col) * 2;
        VGA_BASE[pos]     = ' ';
        VGA_BASE[pos + 1] = blank;
    }
    cur_row = ROWS - 1;
}

void clear_screen(void) {
    unsigned char attr = vga_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int i = 0; i < ROWS * COLS; i++) {
        VGA_BASE[i * 2]     = ' ';
        VGA_BASE[i * 2 + 1] = attr;
    }
    cur_col = 0;
    cur_row = 0;
}

void kputchar_color(char c, unsigned char fg, unsigned char bg) {
    unsigned char attr = vga_color(fg, bg);
    if (c == '\n') {
        cur_col = 0;
        cur_row++;
    } else if (c == '\r') {
        cur_col = 0;
    } else if (c == '\t') {
        cur_col = (cur_col + 8) & ~7;
    } else {
        int pos = (cur_row * COLS + cur_col) * 2;
        VGA_BASE[pos]     = (unsigned char)c;
        VGA_BASE[pos + 1] = attr;
        cur_col++;
    }
    if (cur_col >= COLS) { cur_col = 0; cur_row++; }
    if (cur_row >= ROWS) scroll_up();
}

void kputchar(char c) {
    kputchar_color(c, VGA_LIGHT_GREY, VGA_BLACK);
}

void kprint(const char *str) {
    while (*str) kputchar(*str++);
}

void kprint_color(const char *str, unsigned char fg, unsigned char bg) {
    while (*str) kputchar_color(*str++, fg, bg);
}

void kprint_hex(unsigned int n) {
    const char hex[] = "0123456789ABCDEF";
    char buf[9];
    buf[8] = '\0';
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[n & 0xF];
        n >>= 4;
    }
    int start = 0;
    while (start < 7 && buf[start] == '0') start++;
    kprint("0x");
    kprint(buf + start);
}

void kprint_dec(unsigned int n) {
    if (n == 0) { kputchar('0'); return; }
    char buf[11];
    int i = 10;
    buf[10] = '\0';
    while (n > 0) {
        buf[--i] = (char)('0' + n % 10);
        n /= 10;
    }
    kprint(buf + i);
}
