#pragma once
#include "../include/types.h"

/* FAT32 디렉토리 엔트리 (32바이트 short-name) */
typedef struct {
    uint8_t  name[11];          /* "FOO     TXT" 형태 (8.3, 공백 패딩) */
    uint8_t  attr;              /* 0x10=DIR, 0x0F=LFN, 0x08=VolLabel  */
    uint8_t  ntres;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t cluster_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t cluster_lo;
    uint32_t size;
} __attribute__((packed)) fat32_dirent_t;

#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_LFN       0x0F

typedef void (*fat32_visitor_t)(const fat32_dirent_t *e, void *ctx);

/* 지정된 ATA 드라이브에서 FAT32 BPB를 읽어 마운트한다. 성공 0. */
int  fat32_mount(uint8_t drive);
int  fat32_is_mounted(void);

/* 루트 디렉토리의 시작 클러스터를 반환 */
uint32_t fat32_root_cluster(void);

/* start_cluster 가 가리키는 디렉토리의 모든 유효 엔트리를 visit() 호출.
   LFN/볼륨라벨/삭제됨 항목은 자동 스킵. */
void fat32_listdir(uint32_t start_cluster, fat32_visitor_t visit, void *ctx);

/* 루트에서 8.3 이름(case-insensitive, 예: "hello.txt")으로 파일 검색.
   찾으면 *out 에 엔트리 복사 후 0 반환. 없으면 -1. */
int  fat32_find_in_root(const char *name83, fat32_dirent_t *out);

/* 디렉토리 엔트리가 가리키는 파일을 최대 max 바이트만큼 buf 에 복사.
   복사된 바이트 수 반환, 실패 시 -1. */
int  fat32_read_file(const fat32_dirent_t *e, void *buf, uint32_t max);
