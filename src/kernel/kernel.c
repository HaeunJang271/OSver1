#include "drivers/screen.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/isr.h"
#include "cpu/pic.h"

static void print_banner(void) {
    kprint_color("  __  __       ___  ____  \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" |  \\/  |_   _/ _ \\/ ___| \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" | |\\/| | | | | | | \\___ \\ \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" | |  | | |_| | |_| |___) |\n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" |_|  |_|\\__, |\\___/|____/ \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color("          |___/       v0.2  \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint("\n");
}

static void ok(const char *msg) {
    kprint_color("[ OK ] ", VGA_LIGHT_GREEN, VGA_BLACK);
    kprint(msg);
    kprint("\n");
}

void kmain(void) {
    clear_screen();
    print_banner();

    gdt_init();  ok("GDT loaded (null / kernel code / kernel data)");
    pic_init();  ok("PIC remapped  - IRQs mapped to INT 32-47");
    idt_init();  ok("IDT loaded    - 32 exceptions + 16 IRQ gates");

    __asm__ volatile ("sti");
    ok("Interrupts enabled");

    kprint("\n");
    kprint_color("MyOS v0.2 - system ready\n", VGA_YELLOW, VGA_BLACK);
    kprint("Unhandled exceptions will be caught and reported.\n");

    for (;;) __asm__ volatile ("hlt");
}
