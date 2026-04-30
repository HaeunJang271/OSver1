#include "kheap.h"
#include "../drivers/screen.h"

/* ── 설계 메모 ──────────────────────────────────────────────────────────────
   - 256 KB BSS 슬랩 위에 doubly-linked freelist 를 구성한다.
   - 모든 블록(used + free)이 prev/next 로 메모리 순서대로 연결된다.
     덕분에 kfree() 에서 인접 free 블록과의 coalesce 를 O(1) 에 처리한다.
   - 슬랩이 부족할 때 PMM 페이지를 받아 새 region 으로 확장하는 sbrk 식
     운영도 가능하지만, 인접하지 않은 region 끼리 coalesce 가 깨지므로
     이번 단계에서는 정적 슬랩 한 개만 운용한다 (TODO).
   - 학습 목적이라 magic/used 를 별도 필드로 둬서 디버깅이 쉽게 한다.
     실전 allocator 라면 size 의 LSB 에 used 비트를 묶어 헤더를 8바이트로
     줄이는 것이 표준. */

#define KHEAP_SIZE_KB 256u
#define KHEAP_MAGIC   0xC0FFEE42u
#define KHEAP_ALIGN   8u

typedef struct kheap_block {
    uint32_t            magic;
    uint32_t            size;     /* payload 크기 (헤더 제외) */
    uint32_t            used;
    struct kheap_block *prev;
    struct kheap_block *next;
} kheap_block_t;

#define HDR_SIZE ((uint32_t)sizeof(kheap_block_t))

static uint8_t        heap_storage[KHEAP_SIZE_KB * 1024]
                          __attribute__((aligned(8)));
static kheap_block_t *head = (kheap_block_t *)0;

/* ── 헬퍼 ────────────────────────────────────────────────────────────────── */

static inline uint32_t align_up(uint32_t v, uint32_t a) {
    return (v + a - 1u) & ~(a - 1u);
}

static void kheap_panic(const char *msg) {
    kprint_color("\nKHEAP PANIC: ", VGA_WHITE, VGA_RED);
    kprint(msg); kprint("\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

/* ── 초기화 — 슬랩 전체를 한 개의 거대한 free 블록으로 만든다 ─────────────── */

void kheap_init(void) {
    head = (kheap_block_t *)heap_storage;
    head->magic = KHEAP_MAGIC;
    head->size  = (uint32_t)sizeof(heap_storage) - HDR_SIZE;
    head->used  = 0;
    head->prev  = (kheap_block_t *)0;
    head->next  = (kheap_block_t *)0;
}

/* ── kmalloc — first fit + split ────────────────────────────────────────── */

void *kmalloc(uint32_t size) {
    if (size == 0) return (void *)0;
    size = align_up(size, KHEAP_ALIGN);

    for (kheap_block_t *b = head; b; b = b->next) {
        if (b->magic != KHEAP_MAGIC) kheap_panic("corrupted block");
        if (b->used)             continue;
        if (b->size < size)      continue;

        /* 충분히 크면 split. 새 블록이 헤더+최소 alignment 한 칸은 넘어야
           split 의 의미가 있다 (안 그러면 fragmentation). */
        uint32_t leftover = b->size - size;
        if (leftover >= HDR_SIZE + KHEAP_ALIGN) {
            kheap_block_t *n =
                (kheap_block_t *)((uint8_t *)b + HDR_SIZE + size);
            n->magic = KHEAP_MAGIC;
            n->size  = leftover - HDR_SIZE;
            n->used  = 0;
            n->prev  = b;
            n->next  = b->next;
            if (b->next) b->next->prev = n;
            b->next = n;
            b->size = size;
        }
        b->used = 1;
        return (void *)((uint8_t *)b + HDR_SIZE);
    }
    return (void *)0;          /* OOM */
}

void *kcalloc(uint32_t n, uint32_t size) {
    uint32_t total = n * size;
    void *p = kmalloc(total);
    if (!p) return p;
    uint8_t *bp = (uint8_t *)p;
    for (uint32_t i = 0; i < total; i++) bp[i] = 0;
    return p;
}

/* ── kfree — coalesce with prev/next free neighbors ─────────────────────── */

void kfree(void *ptr) {
    if (!ptr) return;
    kheap_block_t *b =
        (kheap_block_t *)((uint8_t *)ptr - HDR_SIZE);

    if (b->magic != KHEAP_MAGIC) kheap_panic("kfree: bad pointer");
    if (!b->used)                kheap_panic("kfree: double free");
    b->used = 0;

    /* 다음 블록과 합치기 — 메모리상 인접하므로 단순 size 가산 */
    if (b->next && !b->next->used) {
        kheap_block_t *n = b->next;
        b->size += HDR_SIZE + n->size;
        b->next  = n->next;
        if (n->next) n->next->prev = b;
        n->magic = 0;          /* 헤더 무효화 (디버깅 도움) */
    }
    /* 이전 블록과 합치기 */
    if (b->prev && !b->prev->used) {
        kheap_block_t *p = b->prev;
        p->size += HDR_SIZE + b->size;
        p->next  = b->next;
        if (b->next) b->next->prev = p;
        b->magic = 0;
    }
}

/* ── 통계 — 셸 `heap` 명령용 ─────────────────────────────────────────────── */

void kheap_get_stats(kheap_stats_t *s) {
    s->total_bytes  = (uint32_t)sizeof(heap_storage);
    s->used_bytes   = 0;
    s->free_bytes   = 0;
    s->blocks_total = 0;
    s->blocks_used  = 0;
    s->blocks_free  = 0;
    s->largest_free = 0;

    for (kheap_block_t *b = head; b; b = b->next) {
        if (b->magic != KHEAP_MAGIC) kheap_panic("stats: corruption");
        s->blocks_total++;
        if (b->used) {
            s->used_bytes += b->size;
            s->blocks_used++;
        } else {
            s->free_bytes += b->size;
            s->blocks_free++;
            if (b->size > s->largest_free) s->largest_free = b->size;
        }
    }
}
