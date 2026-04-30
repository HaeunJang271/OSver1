#include "paging.h"
#include "pmm.h"
#include "../cpu/isr.h"
#include "../drivers/screen.h"

/* ── 부트스트랩 PD/PT (BSS, 4KB 정렬) ─────────────────────────────────────── */
/* 첫 4MB 를 1:1(identity) 매핑하기 위한 페이지 테이블 1개 + 디렉토리 1개.
   변수 자체에 4KB 정렬을 강제하므로, BSS 시작이 4KB 정렬되지 않아도
   컴파일러/링커가 변수 위치를 4KB 경계로 맞춰준다. */
static uint32_t page_directory[1024]    __attribute__((aligned(4096)));
static uint32_t first_page_table[1024]  __attribute__((aligned(4096)));

/* ── 저수준 헬퍼 ──────────────────────────────────────────────────────────── */

static inline void load_cr3(uint32_t pd_phys) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pd_phys));
}

static inline void enable_paging(void) {
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;            /* CR0.PG */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));
}

static inline uint32_t read_cr2(void) {
    uint32_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

static inline void invlpg(uint32_t virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

/* ── 페이지 폴트 핸들러 (벡터 14) ─────────────────────────────────────────── */

static void page_fault_handler(registers_t *regs) {
    uint32_t fault_addr = read_cr2();
    uint32_t err = regs->err_code;

    kprint_color("\n*** PAGE FAULT ***\n", VGA_WHITE, VGA_RED);

    kprint_color("addr=", VGA_LIGHT_GREY, VGA_BLACK);
    kprint_hex(fault_addr);
    kprint_color("  eip=", VGA_LIGHT_GREY, VGA_BLACK);
    kprint_hex(regs->eip);
    kprint_color("  err=", VGA_LIGHT_GREY, VGA_BLACK);
    kprint_hex(err);
    kprint("\n  reason: ");

    /* err_code 비트 의미 (Intel SDM Vol.3 §4.7) */
    kprint((err & 0x1) ? "protection-violation" : "non-present-page");
    kprint((err & 0x2) ? " | write" : " | read");
    kprint((err & 0x4) ? " | user-mode"   : " | kernel-mode");
    if (err & 0x8)  kprint(" | reserved-bit-set");
    if (err & 0x10) kprint(" | instruction-fetch");
    kprint("\nSystem halted.\n");

    for (;;) __asm__ volatile ("cli; hlt");
}

/* ── PT 조회/생성 ────────────────────────────────────────────────────────── */

/* 가상 주소가 속한 페이지 테이블의 가상 포인터를 돌려준다.
   페이징을 켠 시점에도 첫 4MB 는 identity map 이므로, 우리는 PT의
   "물리 주소"를 그대로 가상 주소로 사용해 접근할 수 있다(PT가 첫 4MB
   안에 있을 때 한정). 새 PT를 PMM에서 받을 때도 동일 가정이 성립하려면
   초기 단계에서는 PT가 첫 4MB 안에 떨어져야 한다.

   create=1 이면 PT 가 없을 때 PMM 으로 새로 만든다. */
static uint32_t *get_pt(uint32_t virt, int create) {
    uint32_t pdi = PAGE_DIR_INDEX(virt);
    uint32_t pde = page_directory[pdi];

    if (pde & PAGE_PRESENT) {
        return (uint32_t *)(pde & 0xFFFFF000u);
    }
    if (!create) return 0;

    void *pt = pmm_alloc();
    if (!pt) return 0;

    uint32_t *new_pt = (uint32_t *)pt;
    for (int i = 0; i < 1024; i++) new_pt[i] = 0;

    page_directory[pdi] = ((uint32_t)pt) | PAGE_PRESENT | PAGE_RW;
    return new_pt;
}

/* ── 공개 API ─────────────────────────────────────────────────────────────── */

int vmm_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    if ((virt | phys) & 0xFFF) return -1;       /* 4KB 정렬 검사 */

    uint32_t *pt = get_pt(virt, 1);
    if (!pt) return -1;

    pt[PAGE_TBL_INDEX(virt)] = (phys & 0xFFFFF000u) | (flags & 0xFFF) | PAGE_PRESENT;
    invlpg(virt);
    return 0;
}

void vmm_unmap(uint32_t virt) {
    uint32_t *pt = get_pt(virt, 0);
    if (!pt) return;
    pt[PAGE_TBL_INDEX(virt)] = 0;
    invlpg(virt);
}

uint32_t vmm_get_phys(uint32_t virt) {
    uint32_t *pt = get_pt(virt, 0);
    if (!pt) return 0xFFFFFFFFu;
    uint32_t pte = pt[PAGE_TBL_INDEX(virt)];
    if (!(pte & PAGE_PRESENT)) return 0xFFFFFFFFu;
    return (pte & 0xFFFFF000u) | PAGE_OFFSET(virt);
}

/* ── 초기화 ──────────────────────────────────────────────────────────────── */

void paging_init(void) {
    /* 1) PD 전체를 비-매핑으로 초기화 */
    for (int i = 0; i < 1024; i++) page_directory[i] = 0;

    /* 2) 첫 4MB identity map 을 위해 first_page_table 채우기.
          PTE = phys | RW | PRESENT  (커널 전용이므로 USER 비트 없음) */
    for (uint32_t i = 0; i < 1024; i++) {
        uint32_t phys = i * 0x1000;
        first_page_table[i] = phys | PAGE_RW | PAGE_PRESENT;
    }

    /* 3) PD[0] → first_page_table 연결 (가상 [0, 4MB) 커버) */
    page_directory[0] = ((uint32_t)first_page_table) | PAGE_RW | PAGE_PRESENT;

    /* 4) PF 핸들러 등록 (PG 비트 켜기 전에 등록해두면 안전) */
    isr_register(14, page_fault_handler);

    /* 5) CR3 적재 → CR0.PG 활성화 */
    load_cr3((uint32_t)page_directory);
    enable_paging();
}
