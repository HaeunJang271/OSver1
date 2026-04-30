#include "task.h"
#include "../mem/kheap.h"
#include "../drivers/screen.h"
#include "../cpu/tss.h"
#include "../cpu/gdt.h"

/* ── 외부 어셈블리 ──────────────────────────────────────────────────────── */
extern void context_switch(uint32_t **old_esp_ptr, uint32_t *new_esp);
extern void task_start_trampoline(void);
extern void user_iret_trampoline(void);

/* ── 전역 상태 ──────────────────────────────────────────────────────────── */
static task_t        *g_current     = (task_t *)0;
static int            g_active      = 0;
static volatile int   g_need_resched = 0;
static uint32_t       g_next_id     = 0;

/* ── 작은 헬퍼 ──────────────────────────────────────────────────────────── */
static void str_copy_n(char *dst, const char *src, int max) {
    int i;
    for (i = 0; i < max - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static void panic_t(const char *msg) {
    kprint_color("\nTASK PANIC: ", VGA_WHITE, VGA_RED);
    kprint(msg); kprint("\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

/* ── ready 큐 조작 ─────────────────────────────────────────────────────── */

static void ready_insert_after_current(task_t *t) {
    /* current 다음에 끼워 넣기 — RR 스케줄러는 next 로 진행하므로
       새 task 가 가장 먼저 돌게 된다. */
    t->next = g_current->next;
    g_current->next = t;
}

static void ready_remove(task_t *target) {
    /* circular list 에서 target 노드 제거.
       호출자는 target == g_current 가 아닌 경우만 부른다. */
    task_t *p = target->next;
    while (p->next != target) p = p->next;
    p->next = target->next;
}

/* ZOMBIE 노드를 ready 큐에서 떼고 메모리 해제 */
static void reap(task_t *t) {
    ready_remove(t);
    if (t->stack_base)      kfree(t->stack_base);
    if (t->user_stack_base) kfree(t->user_stack_base);
    kfree(t);
}

/* ── 부팅 ─────────────────────────────────────────────────────────────── */

void tasking_init(void) {
    task_t *boot = (task_t *)kmalloc(sizeof(task_t));
    if (!boot) panic_t("tasking_init: kmalloc failed");

    boot->id               = g_next_id++;
    str_copy_n(boot->name, "boot", TASK_NAME_LEN);
    boot->state            = TASK_RUNNING;
    boot->esp              = 0;                /* 첫 yield 때 자동 저장 */
    boot->stack_base       = (void *)0;        /* BSP 스택은 우리 게 아님 */
    boot->stack_size       = 0;
    boot->kernel_stack_top = 0;
    boot->is_user          = 0;
    boot->user_stack_base  = (void *)0;
    boot->ticks_run        = 0;
    boot->slice_left       = TASK_TIME_SLICE;
    boot->next             = boot;             /* 자기 자신 */

    g_current = boot;
    g_active  = 1;
}

/* ── task_create ────────────────────────────────────────────────────────
   새 task 의 커널 스택을 다음 구조로 셋업한다 (위쪽이 high addr):

       (entry 함수 포인터)           ← 트램폴린이 pop 해서 호출
       task_start_trampoline        ← context_switch 의 ret 점프 대상
       0  (ebp)
       0  (ebx)
       0  (esi)
       0  (edi)                      ← 저장된 esp
*/
task_t *task_create(const char *name, void (*entry)(void)) {
    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    if (!t) return (task_t *)0;

    uint8_t *stack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!stack) { kfree(t); return (task_t *)0; }

    t->id               = g_next_id++;
    str_copy_n(t->name, name, TASK_NAME_LEN);
    t->state            = TASK_READY;
    t->stack_base       = stack;
    t->stack_size       = TASK_STACK_SIZE;
    t->kernel_stack_top = (uint32_t)stack + TASK_STACK_SIZE;
    t->is_user          = 0;
    t->user_stack_base  = (void *)0;
    t->ticks_run        = 0;
    t->slice_left       = TASK_TIME_SLICE;

    /* 스택 셋업 — top 부터 내려가며 푸시 */
    uint32_t *sp = (uint32_t *)(stack + TASK_STACK_SIZE);
    *--sp = (uint32_t)entry;                    /* trampoline 의 pop eax 대상 */
    *--sp = (uint32_t)task_start_trampoline;    /* context_switch 의 ret 대상 */
    *--sp = 0;   /* ebp */
    *--sp = 0;   /* ebx */
    *--sp = 0;   /* esi */
    *--sp = 0;   /* edi */
    t->esp = (uint32_t)sp;

    /* ready 큐에 끼워넣기 — 인터럽트 차단 후 안전하게 */
    __asm__ volatile ("cli");
    if (!g_active) {
        /* tasking_init 을 안 불렀으면 큐가 없음 — 그냥 자기 자신 */
        t->next = t;
        g_current = t;
        g_active  = 1;
    } else {
        ready_insert_after_current(t);
    }
    __asm__ volatile ("sti");

    return t;
}

/* ── Ring 3 user task 생성 ───────────────────────────────────────────────
   커널 스택 위에 미리 다음 모습을 박아둔다 (high → low):

     SS3 (0x23)
     ESP3 (user stack top)
     EFLAGS (0x202)
     CS3 (0x1B)
     EIP (entry)
     ── iret 5-frame ──
     user_iret_trampoline      ← context_switch 의 ret 점프 대상
     0  (ebp)
     0  (ebx)
     0  (esi)
     0  (edi)                  ← 저장된 esp                              */
task_t *task_create_user(const char *name, void (*entry)(void)) {
    task_t  *t      = (task_t *)kmalloc(sizeof(task_t));
    if (!t) return (task_t *)0;
    uint8_t *kstack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    uint8_t *ustack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!kstack || !ustack) {
        if (kstack) kfree(kstack);
        if (ustack) kfree(ustack);
        kfree(t);
        return (task_t *)0;
    }

    uint32_t kstack_top = (uint32_t)kstack + TASK_STACK_SIZE;
    uint32_t ustack_top = ((uint32_t)ustack + TASK_STACK_SIZE) & ~0xFu;

    t->id               = g_next_id++;
    str_copy_n(t->name, name, TASK_NAME_LEN);
    t->state            = TASK_READY;
    t->stack_base       = kstack;
    t->stack_size       = TASK_STACK_SIZE;
    t->kernel_stack_top = kstack_top;
    t->is_user          = 1;
    t->user_stack_base  = ustack;
    t->ticks_run        = 0;
    t->slice_left       = TASK_TIME_SLICE;

    /* 1) iret 5-frame */
    uint32_t *sp = (uint32_t *)kstack_top;
    *--sp = GDT_UDATA_SEL;            /* SS3 (RPL=3 이미 포함) */
    *--sp = ustack_top;               /* ESP3 */
    *--sp = 0x202;                    /* EFLAGS: IF=1 + reserved bit 1 */
    *--sp = GDT_UCODE_SEL;            /* CS3 */
    *--sp = (uint32_t)entry;          /* EIP */

    /* 2) ret 점프 대상 + callee-saved */
    *--sp = (uint32_t)user_iret_trampoline;
    *--sp = 0;   /* ebp */
    *--sp = 0;   /* ebx */
    *--sp = 0;   /* esi */
    *--sp = 0;   /* edi */
    t->esp = (uint32_t)sp;

    __asm__ volatile ("cli");
    if (!g_active) {
        t->next   = t;
        g_current = t;
        g_active  = 1;
    } else {
        ready_insert_after_current(t);
    }
    __asm__ volatile ("sti");
    return t;
}

/* ── task_exit ──────────────────────────────────────────────────────── */

void task_exit(void) {
    __asm__ volatile ("cli");
    g_current->state = TASK_ZOMBIE;
    /* yield 안에서 ZOMBIE 본인은 reap 안 한다 (자기 스택 위에서 free 위험).
       다음 task 가 자기 다음 ZOMBIE 들을 청소하게 된다. */
    __asm__ volatile ("sti");

    task_yield();
    /* unreachable — ZOMBIE 는 절대 다시 schedule 안 됨 */
    panic_t("task_exit: returned from yield (ZOMBIE rescheduled)");
    while (1) __asm__ volatile ("cli; hlt");
}

/* ── 스케줄러 ──────────────────────────────────────────────────────── */

static task_t *pick_next(task_t *from) {
    /* from 다음부터 한 바퀴 돌아 READY/RUNNING task 찾기.
       지나가다 ZOMBIE 만나면 reap. (단, 자기 자신은 건드리지 않는다) */
    task_t *cand = from->next;
    while (cand != from) {
        if (cand->state == TASK_ZOMBIE) {
            task_t *dead = cand;
            cand = cand->next;
            reap(dead);
            continue;
        }
        if (cand->state == TASK_READY || cand->state == TASK_RUNNING)
            return cand;
        cand = cand->next;
    }
    /* 한 바퀴 돌아왔는데 다른 후보 없음 → from 자신 (살아있다는 가정) */
    return from;
}

void task_yield(void) {
    if (!g_active) return;

    __asm__ volatile ("cli");
    task_t *prev = g_current;
    task_t *next = pick_next(prev);

    if (next == prev) {
        /* ZOMBIE 가 아니면 그대로 진행 (선택권 없음) */
        if (prev->state != TASK_ZOMBIE) {
            __asm__ volatile ("sti");
            return;
        }
        /* prev 가 ZOMBIE 인데 다른 후보가 없다 — idle 진입.
           시스템 마지막 task 라는 뜻. halt. */
        kprint_color("\n[scheduler] no runnable tasks. halting.\n",
                     VGA_LIGHT_RED, VGA_BLACK);
        for (;;) __asm__ volatile ("cli; hlt");
    }

    /* prev 상태 갱신 */
    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    prev->slice_left = TASK_TIME_SLICE;
    next->slice_left = TASK_TIME_SLICE;

    g_current = next;

    /* TSS.esp0 갱신 — next 가 ring 3 task 라면 다음 syscall/IRQ 진입 시
       이 값으로 커널 스택이 자동 전환된다. ring 0 task 라도 무해하므로
       항상 갱신해 일관성 유지. (kernel_stack_top == 0 인 boot task 는 skip) */
    if (next->kernel_stack_top) tss_set_esp0(next->kernel_stack_top);

    /* context_switch 가 자체적으로 esp 저장/복원. */
    context_switch((uint32_t **)&prev->esp, (uint32_t *)next->esp);
    /* 여기로 돌아오면 prev 가 다시 schedule 된 것. cli 상태 회복. */
    __asm__ volatile ("sti");
}

/* ── PIT tick → 슬라이스 회계 ────────────────────────────────────────── */

void task_tick(void) {
    if (!g_active) return;
    g_current->ticks_run++;
    if (g_current->slice_left > 0) g_current->slice_left--;
    if (g_current->slice_left == 0) g_need_resched = 1;
}

int task_should_resched(void) {
    if (!g_need_resched) return 0;
    g_need_resched = 0;
    return 1;
}

/* ── 조회 ─────────────────────────────────────────────────────────────── */

task_t *task_current(void)  { return g_current; }
int     tasking_active(void){ return g_active; }

int task_list_snapshot(task_info_t *out, int max) {
    if (!g_active || !out || max <= 0) return 0;
    __asm__ volatile ("cli");

    int n = 0;
    task_t *t = g_current;
    do {
        if (n >= max) break;
        out[n].id        = t->id;
        out[n].state     = t->state;
        out[n].ticks_run = t->ticks_run;
        out[n].is_current = (t == g_current);
        for (int i = 0; i < TASK_NAME_LEN; i++) out[n].name[i] = t->name[i];
        n++;
        t = t->next;
    } while (t != g_current);

    __asm__ volatile ("sti");
    return n;
}
