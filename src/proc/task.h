#pragma once
#include "../include/types.h"

/* forward decl — elf.h 가 정의 */
struct user_image;

/* ── 멀티태스킹 ───────────────────────────────────────────────────────────
   라운드로빈 스케줄러 + 협력적/선점 양방향 지원.
     - task_yield()    : 자발적 양보
     - task_tick()     : PIT IRQ 에서 호출, 슬라이스 만료 시 need_resched 세트
     - task_should_resched() : isr.c 가 IRQ 처리 끝부분에서 체크
   컨텍스트 스위치는 src/proc/switch.asm 의 context_switch 가 담당. */

#define TASK_NAME_LEN     16
#define TASK_STACK_SIZE   0x4000     /* 16 KB per task */
#define TASK_TIME_SLICE   5          /* 5 PIT ticks ≈ 50ms */

typedef enum {
    TASK_READY   = 0,
    TASK_RUNNING = 1,
    TASK_ZOMBIE  = 2,
} task_state_t;

typedef struct task {
    uint32_t      id;
    char          name[TASK_NAME_LEN];
    task_state_t  state;

    /* 스케줄링 컨텍스트 */
    uint32_t      esp;              /* saved kernel stack pointer */
    void         *stack_base;       /* kmalloc'd kernel stack     */
    uint32_t      stack_size;
    uint32_t      kernel_stack_top; /* TSS.esp0 로 복사될 값      */

    /* User mode 전용 */
    int           is_user;          /* 0 = ring 0, 1 = ring 3 task */
    void         *user_stack_base;  /* ring 3 stack (kmalloc)     */
    struct user_image *image;       /* exec 으로 적재된 ELF 이미지 (NULL=없음) */

    /* 통계 */
    uint32_t      ticks_run;
    uint32_t      slice_left;

    struct task  *next;             /* circular ready list */
} task_t;

/* ── 부팅 ─────────────────────────────────────────────────────────────── */
/* 현재 실행 중인 흐름(=셸 진입 직전의 컨텍스트)을 task 0 "boot" 으로 등록.
   호출 후엔 멀티태스킹이 활성화된다. */
void   tasking_init(void);

/* ── task 라이프사이클 ────────────────────────────────────────────────── */
task_t *task_create(const char *name, void (*entry)(void));

/* Ring 3 user task 생성 — entry 코드는 식별 매핑된 첫 4MB 안에 있어야 한다.
   별도 user 스택을 kmalloc 해서 ring 3 가 사용한다. */
task_t *task_create_user(const char *name, void (*entry)(void));

/* exec — ELF 이미지로 적재된 ring 3 task. image 는 task->image 로 보관되어
   reap 시 elf_unload 가 자동 호출된다. entry 는 image->entry 를 사용. */
task_t *task_create_user_image(const char *name, struct user_image *image);

/* 현재 task 종료 — 호출 후 절대 반환하지 않는다. 다음 task 로 점프 */
void   task_exit(void) __attribute__((noreturn));

/* 자발적 양보 */
void   task_yield(void);

/* PIT IRQ 가 매 틱 호출. 슬라이스 만료 시 need_resched 플래그 set */
void   task_tick(void);

/* isr.c 가 IRQ 처리 후 호출 — 1 이면 isr.c 가 task_yield() 호출 */
int    task_should_resched(void);

/* ── 조회 ─────────────────────────────────────────────────────────────── */
task_t *task_current(void);
int    tasking_active(void);

typedef struct {
    uint32_t     id;
    char         name[TASK_NAME_LEN];
    task_state_t state;
    uint32_t     ticks_run;
    int          is_current;
} task_info_t;

/* ready 큐의 모든 task 를 out 배열에 채워 개수 반환 */
int    task_list_snapshot(task_info_t *out, int max);
