#pragma once

/* VGA text mode color palette (foreground and background) */
#define VGA_BLACK         0
#define VGA_BLUE          1
#define VGA_GREEN         2
#define VGA_CYAN          3
#define VGA_RED           4
#define VGA_MAGENTA       5
#define VGA_BROWN         6
#define VGA_LIGHT_GREY    7
#define VGA_DARK_GREY     8
#define VGA_LIGHT_BLUE    9
#define VGA_LIGHT_GREEN   10
#define VGA_LIGHT_CYAN    11
#define VGA_LIGHT_RED     12
#define VGA_LIGHT_MAGENTA 13
#define VGA_YELLOW        14
#define VGA_WHITE         15

static inline unsigned char vga_color(unsigned char fg, unsigned char bg) {
    return (unsigned char)(fg | (bg << 4));
}

void clear_screen(void);
void kputchar(char c);
void kputchar_color(char c, unsigned char fg, unsigned char bg);
void kprint(const char *str);
void kprint_color(const char *str, unsigned char fg, unsigned char bg);
void kprint_hex(unsigned int n);
void kprint_dec(unsigned int n);
void kget_cursor(int *col, int *row);
