#include "pmm.h"

/* ── e820 메모리 맵 (부트로더가 0x500에 저장) ────────────────────────────── */
#define E820_COUNT ((volatile uint16_t *)0x500)
#define E820_TABLE ((e820_entry_t     *)0x502)
#define E820_USABLE 1

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
} __attribute__((packed)) e820_entry_t;

/* ── 비트맵: 1비트 = 4KB 페이지 한 개, 최대 4 GB 커버 ───────────────────── */
#define TOTAL_PAGES  (1u << 20)          /* 4 GB / 4 KB = 1 M 페이지      */
#define BITMAP_WORDS (TOTAL_PAGES / 32)  /* uint32_t 배열 크기 = 128 KB   */

static uint32_t bitmap[BITMAP_WORDS];   /* BSS → kernel_entry가 0으로 초기화 */
static uint32_t free_count  = 0;
static uint32_t total_count = 0;

/* ── 비트맵 헬퍼 ────────────────────────────────────────────────────────── */
static inline void page_set_used(uint32_t p) {
    bitmap[p >> 5] |=  (1u << (p & 31));
}
static inline void page_set_free(uint32_t p) {
    bitmap[p >> 5] &= ~(1u << (p & 31));
}
static inline int page_is_free(uint32_t p) {
    return !(bitmap[p >> 5] & (1u << (p & 31)));
}

/* ── 영역 단위 조작 ─────────────────────────────────────────────────────── */
static void region_free(uint64_t base, uint64_t len) {
    uint32_t first = (uint32_t)(base / PAGE_SIZE);
    uint32_t last  = (uint32_t)((base + len) / PAGE_SIZE);
    if (last  > TOTAL_PAGES) last = TOTAL_PAGES;
    for (uint32_t p = first; p < last; p++) {
        if (!page_is_free(p)) {
            page_set_free(p);
            free_count++;
            total_count++;
        }
    }
}

static void region_reserve(uint64_t base, uint64_t len) {
    uint32_t first = (uint32_t)(base / PAGE_SIZE);
    uint32_t last  = (uint32_t)((base + len + PAGE_SIZE - 1) / PAGE_SIZE);
    if (last  > TOTAL_PAGES) last = TOTAL_PAGES;
    for (uint32_t p = first; p < last; p++) {
        if (page_is_free(p)) {
            page_set_used(p);
            free_count--;
        }
    }
}

/* ── 공개 API ────────────────────────────────────────────────────────────── */
void pmm_init(void) {
    free_count  = 0;
    total_count = 0;

    /* 전체 비트맵을 "사용중"으로 초기화 */
    for (uint32_t i = 0; i < BITMAP_WORDS; i++)
        bitmap[i] = 0xFFFFFFFF;

    /* e820 usable 영역을 가용으로 표시
       매크로를 직접 인덱싱하면 GCC -Warray-bounds가 false positive 경고를
       내므로, 일단 로컬 포인터 변수에 담아 사용한다. */
    volatile uint16_t *e820_count = E820_COUNT;
    e820_entry_t      *e820_table = E820_TABLE;
    uint16_t count = *e820_count;
    for (uint16_t i = 0; i < count; i++) {
        if (e820_table[i].type == E820_USABLE)
            region_free(e820_table[i].base, e820_table[i].length);
    }

    /* 첫 1 MB(커널, 부트로더, BIOS, e820 버퍼 포함)는 항상 예약 */
    region_reserve(0, 0x100000);
}

void *pmm_alloc(void) {
    /* 비트맵을 word 단위로 훑어 빈 페이지를 O(N/32)에 찾는다 */
    for (uint32_t w = 0; w < BITMAP_WORDS; w++) {
        if (bitmap[w] == 0xFFFFFFFF) continue;
        for (int b = 0; b < 32; b++) {
            uint32_t p = w * 32 + b;
            if (page_is_free(p)) {
                page_set_used(p);
                free_count--;
                return (void *)(p * PAGE_SIZE);
            }
        }
    }
    return 0;   /* 메모리 부족 */
}

void pmm_free(void *addr) {
    uint32_t p = (uint32_t)addr / PAGE_SIZE;
    if (p >= TOTAL_PAGES || page_is_free(p)) return;
    page_set_free(p);
    free_count++;
}

uint32_t pmm_free_pages(void)  { return free_count;  }
uint32_t pmm_total_pages(void) { return total_count; }
