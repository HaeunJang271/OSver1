#include "drivers/screen.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/isr.h"
#include "cpu/pic.h"
#include "cpu/tss.h"
#include "drivers/keyboard.h"
#include "drivers/ata.h"
#include "mem/pmm.h"
#include "mem/paging.h"
#include "mem/kheap.h"
#include "fs/fat32.h"
#include "proc/task.h"
#include "shell/shell.h"

static void print_banner(void) {
    kprint_color("  __  __       ___  ____  \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" |  \\/  |_   _/ _ \\/ ___| \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" | |\\/| | | | | | | \\___ \\ \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" | |  | | |_| | |_| |___) |\n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color(" |_|  |_|\\__, |\\___/|____/ \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_color("          |___/       v2.2 \n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint("\n");
}

static void ok(const char *msg) {
    kprint_color("[ OK ] ", VGA_LIGHT_GREEN, VGA_BLACK);
    kprint(msg);
    kprint("\n");
}

void kmain(void) {
    clear_screen();
    /* 시리얼은 다른 무엇보다 먼저 — 이후 모든 kprint() 가 호스트 콘솔에 미러링된다 */
    serial_init();
    print_banner();
    ok("Serial COM1 ready (115200 8N1)");

    gdt_init();      ok("GDT loaded (kernel + user segments + TSS slot)");
    tss_init();      ok("TSS loaded (LTR done)");
    pic_init();      ok("PIC remapped");
    idt_init();      ok("IDT loaded (incl. INT 0x80 syscall, DPL=3)");
    keyboard_init(); ok("Keyboard driver ready");
    pmm_init();
    kprint_color("[ OK ] ", VGA_LIGHT_GREEN, VGA_BLACK);
    kprint("PMM ready - free: ");
    kprint_dec(pmm_free_pages() * 4 / 1024);
    kprint(" MB / total: ");
    kprint_dec(pmm_total_pages() * 4 / 1024);
    kprint(" MB\n");

    paging_init();   ok("Paging enabled - first 4 MB identity-mapped");

    kheap_init();    ok("Kernel heap ready (256 KB freelist, 8B align)");

    ata_init();      ok("ATA driver ready (primary bus, polling PIO)");

    if (fat32_mount(ATA_DRIVE_SLAVE) == 0) {
        ok("FAT32 mounted on primary slave");
    } else {
        kprint_color("[WARN] ", VGA_YELLOW, VGA_BLACK);
        kprint("FAT32 mount failed (no data disk?) - 'ls/cat' will be unavailable\n");
    }

    timer_init();    ok("PIT 100 Hz timer running (IRQ 0)");

    tasking_init();  ok("Multitasking active (round-robin, 50ms slice)");

    __asm__ volatile ("sti");
    ok("Interrupts enabled");

    kprint("\n");
    shell_run();    /* 셸로 제어권 넘기기 — task 0 으로 자동 등록됨 */
}
