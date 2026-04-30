#include "drivers/screen.h"

static void print_banner(void) {
    kprint_color("  __  __       ___  ____  \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" |  \\/  |_   _/ _ \\/ ___| \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" | |\\/| | | | | | | \\___ \\ \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" | |  | | |_| | |_| |___) |\n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" |_|  |_|\\__, |\\___/|____/ \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color("          |___/       v0.1  \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint("\n");
}

void kmain(void) {
    clear_screen();
    print_banner();

    kprint_color("[ OK ] ", VGA_LIGHT_GREEN, VGA_BLACK);
    kprint("Bootloader complete\n");

    kprint_color("[ OK ] ", VGA_LIGHT_GREEN, VGA_BLACK);
    kprint("32-bit protected mode active\n");

    kprint_color("[ OK ] ", VGA_LIGHT_GREEN, VGA_BLACK);
    kprint("VGA text driver ready\n");

    kprint("\n");
    kprint_color("Kernel loaded at: ", VGA_LIGHT_GREY, VGA_BLACK);
    kprint_hex(0x10000);
    kprint("\n");

    kprint("\n");
    kprint_color("MyOS is running. More features coming soon!\n", VGA_YELLOW, VGA_BLACK);

    for (;;) {}
}
