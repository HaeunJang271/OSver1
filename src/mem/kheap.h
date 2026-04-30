#pragma once
#include "../include/types.h"

/* 커널 힙 — first-fit, doubly-linked, coalescing freelist allocator.
   `kmalloc` 으로 받은 포인터는 8바이트 정렬을 보장한다. */

void  kheap_init(void);
void *kmalloc(uint32_t size);
void *kcalloc(uint32_t n, uint32_t size);
void  kfree(void *ptr);

typedef struct {
    uint32_t total_bytes;     /* 힙 영역 크기 (헤더 포함) */
    uint32_t used_bytes;      /* allocated payload 합계 (헤더 제외)  */
    uint32_t free_bytes;      /* free payload 합계        */
    uint32_t blocks_total;
    uint32_t blocks_used;
    uint32_t blocks_free;
    uint32_t largest_free;    /* 가장 큰 free 블록의 payload 크기 */
} kheap_stats_t;

void kheap_get_stats(kheap_stats_t *s);
