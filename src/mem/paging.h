#pragma once
#include "../include/types.h"

/* ── 페이지 디렉토리 / 페이지 테이블 엔트리 플래그 ─────────────────────────── */
/* x86 32-bit 페이징, 4KB 페이지 기준 */
#define PAGE_PRESENT      0x001  /* 비트 0: 매핑 유효     */
#define PAGE_RW           0x002  /* 비트 1: 0=RO, 1=R/W   */
#define PAGE_USER         0x004  /* 비트 2: 0=커널, 1=유저 */
#define PAGE_PWT          0x008  /* write-through         */
#define PAGE_PCD          0x010  /* cache-disable         */
#define PAGE_ACCESSED     0x020
#define PAGE_DIRTY        0x040  /* PTE에서만             */

#define PAGE_DIR_INDEX(v)  (((uint32_t)(v) >> 22) & 0x3FF)
#define PAGE_TBL_INDEX(v)  (((uint32_t)(v) >> 12) & 0x3FF)
#define PAGE_OFFSET(v)     ((uint32_t)(v) & 0xFFF)

/* ── 공개 API ─────────────────────────────────────────────────────────────── */

/* 부트스트랩 페이지 디렉토리 구성 → CR3 적재 → CR0.PG 활성화 →
   페이지 폴트 ISR(벡터 14) 등록까지 한 번에 처리한다.
   PMM 초기화 이후에 호출해야 한다(새 PT 동적 할당 시 PMM 사용). */
void paging_init(void);

/* 가상 페이지 1개를 물리 페이지에 매핑한다.
   둘 다 4KB 정렬이어야 하며, flags 에는 PAGE_RW 등을 OR 한다.
   해당 PDE 의 PT가 아직 없다면 PMM에서 새로 한 페이지 받아 만든다.
   성공 시 0, 실패(메모리 부족 등) 시 -1. */
int  vmm_map(uint32_t virt, uint32_t phys, uint32_t flags);

/* 가상 페이지 매핑 해제 + TLB 무효화 */
void vmm_unmap(uint32_t virt);

/* 가상 주소가 가리키는 물리 주소를 반환. 매핑 없으면 0xFFFFFFFF. */
uint32_t vmm_get_phys(uint32_t virt);
