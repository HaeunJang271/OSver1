#include "drivers/screen.h"
#include "drivers/keyboard.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/isr.h"
#include "cpu/pic.h"
#include "mem/pmm.h"

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

static void print_mem_info(void) {
    uint32_t free_kb  = pmm_free_pages()  * (PAGE_SIZE / 1024);
    uint32_t total_kb = pmm_total_pages() * (PAGE_SIZE / 1024);
    uint32_t used_kb  = total_kb - free_kb;

    kprint_color("       ", VGA_LIGHT_GREEN, VGA_BLACK);
    kprint("Total: ");  kprint_dec(total_kb / 1024); kprint(" MB  ");
    kprint("Used: ");   kprint_dec(used_kb  / 1024); kprint(" MB  ");
    kprint("Free: ");   kprint_dec(free_kb  / 1024); kprint(" MB\n");
}

static void prompt(void) {
    kprint_color("> ", VGA_LIGHT_GREEN, VGA_BLACK);
}

void kmain(void) {
    clear_screen();
    print_banner();

    gdt_init();      ok("GDT loaded");
    pic_init();      ok("PIC remapped  - IRQs mapped to INT 32-47");
    idt_init();      ok("IDT loaded    - 32 exceptions + 16 IRQ gates");
    keyboard_init(); ok("Keyboard driver ready (PS/2, IRQ1)");
    pmm_init();      ok("PMM initialized");
    print_mem_info();

    __asm__ volatile ("sti");
    ok("Interrupts enabled");

    kprint("\n");
    kprint_color("MyOS v0.4 - type anything below\n\n", VGA_YELLOW, VGA_BLACK);

    for (;;) {
        prompt();

        int pcol, prow;
        kget_cursor(&pcol, &prow);

        for (;;) {
            char c = keyboard_getchar();
            if (c == '\n') {
                kprint("\n");
                break;
            } else if (c == '\b') {
                int ccol, crow;
                kget_cursor(&ccol, &crow);
                if (crow > prow || (crow == prow && ccol > pcol))
                    kputchar('\b');
            } else {
                kputchar(c);
            }
        }
    }
}
