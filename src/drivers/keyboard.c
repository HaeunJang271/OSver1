#include "keyboard.h"
#include "../cpu/isr.h"
#include "../include/io.h"

#define KB_DATA   0x60
#define KB_STATUS 0x64
#define BUF_SIZE  256

/* ── Circular key buffer ─────────────────────────────────────────────────── */
static char buf[BUF_SIZE];
static int  head = 0;   /* write position */
static int  tail = 0;   /* read position  */

static void buf_push(char c) {
    int next = (head + 1) % BUF_SIZE;
    if (next != tail) {
        buf[head] = c;
        head = next;
    }
}

/* ── Scancode set 1 tables (index = scancode) ────────────────────────────── */
static const char sc_normal[58] = {
/*00*/  0,   27, '1','2','3','4','5','6','7','8',
/*0A*/ '9', '0', '-','=','\b','\t','q','w','e','r',
/*14*/ 't', 'y', 'u','i','o','p','[',']','\n', 0,
/*1E*/ 'a', 's', 'd','f','g','h','j','k','l',';',
/*28*/ '\'','`',  0,'\\','z','x','c','v','b','n',
/*32*/ 'm', ',', '.','/',  0,  '*', 0, ' '
};

static const char sc_shifted[58] = {
/*00*/  0,   27, '!','@','#','$','%','^','&','*',
/*0A*/ '(', ')', '_','+','\b','\t','Q','W','E','R',
/*14*/ 'T', 'Y', 'U','I','O','P','{','}','\n', 0,
/*1E*/ 'A', 'S', 'D','F','G','H','J','K','L',':',
/*28*/ '"', '~',  0, '|','Z','X','C','V','B','N',
/*32*/ 'M', '<', '>','?',  0,  '*', 0, ' '
};

/* ── Modifier state ──────────────────────────────────────────────────────── */
static int shift = 0;
static int caps  = 0;

/* ── IRQ1 handler ────────────────────────────────────────────────────────── */
static void kb_irq_handler(registers_t *regs) {
    (void)regs;
    uint8_t sc = inb(KB_DATA);

    if (sc & 0x80) {
        /* Key release */
        sc &= 0x7F;
        if (sc == 0x2A || sc == 0x36) shift = 0;
        return;
    }

    /* Modifier keys */
    if (sc == 0x2A || sc == 0x36) { shift = 1; return; }  /* Shift      */
    if (sc == 0x3A)                { caps ^= 1; return; }  /* Caps Lock  */
    if (sc == 0x1D || sc == 0x38)  {            return; }  /* Ctrl / Alt */

    if (sc >= 58) return;   /* ignore keys beyond our table */

    char c;
    int  use_shift = shift;

    /* Caps Lock toggles shift for letter keys only */
    char base = sc_normal[sc];
    if (caps && base >= 'a' && base <= 'z')
        use_shift ^= 1;

    c = use_shift ? sc_shifted[sc] : sc_normal[sc];
    if (c) buf_push(c);
}

/* ── Public API ──────────────────────────────────────────────────────────── */
void keyboard_init(void) {
    isr_register(33, kb_irq_handler);  /* IRQ1 = INT 33 after PIC remap */
}

int keyboard_haschar(void) {
    return head != tail;
}

char keyboard_getchar(void) {
    while (!keyboard_haschar())
        __asm__ volatile ("hlt");   /* sleep until next interrupt */
    char c = buf[tail];
    tail = (tail + 1) % BUF_SIZE;
    return c;
}
