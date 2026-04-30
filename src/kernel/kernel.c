#include "drivers/screen.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/isr.h"
#include "cpu/pic.h"
#include "drivers/keyboard.h"
#include "mem/pmm.h"
#include "shell/shell.h"

static void print_banner(void) {
    kprint_color("  __  __       ___  ____  \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" |  \\/  |_   _/ _ \\/ ___| \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" | |\\/| | | | | | | \\___ \\ \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" | |  | | |_| | |_| |___) |\n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" |_|  |_|\\__, |\\___/|____/ \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color("          |___/       v0.4  \n", VGA_LIGHT_CYAN, VGA_BLACK);
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

    gdt_init();      ok("GDT loaded");
    pic_init();      ok("PIC remapped");
    idt_init();      ok("IDT loaded");
    keyboard_init(); ok("Keyboard driver ready");
    pmm_init();
    kprint_color("[ OK ] ", VGA_LIGHT_GREEN, VGA_BLACK);
    kprint("PMM ready - free: ");
    kprint_dec(pmm_free_pages() * 4 / 1024);
    kprint(" MB / total: ");
    kprint_dec(pmm_total_pages() * 4 / 1024);
    kprint(" MB\n");

    __asm__ volatile ("sti");
    ok("Interrupts enabled");

    kprint("\n");
    shell_run();    /* 셸로 제어권 넘기기 — 여기서 반환하지 않음 */
}
