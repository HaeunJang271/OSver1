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

/* 현재 디렉토리(cwd) 에서 8.3 이름(case-insensitive)으로 검색.
   찾으면 *out 에 엔트리 복사 후 0 반환. 없으면 -1. */
int  fat32_find(const char *name83, fat32_dirent_t *out);

/* (호환용 별칭) 루트에서 검색. 사실상 cwd가 root 일 때 fat32_find 와 동일. */
int  fat32_find_in_root(const char *name83, fat32_dirent_t *out);

/* 디렉토리 엔트리가 가리키는 파일을 최대 max 바이트만큼 buf 에 복사.
   복사된 바이트 수 반환, 실패 시 -1. */
int  fat32_read_file(const fat32_dirent_t *e, void *buf, uint32_t max);

/* ── 쓰기 API (루트 디렉토리 한정) ─────────────────────────────────────────
   모든 함수는 8.3 이름(case-insensitive 입력 OK, 내부에서 대문자 변환)을
   사용한다. LFN(긴 파일이름)은 만들지 않는다. */

/* 빈 파일을 새로 만든다. 같은 이름이 이미 있으면 -1. 성공 0. */
int fat32_create(const char *name);

/* 파일에 데이터를 덮어쓴다. 파일이 없으면 새로 만들고, 있으면 기존
   클러스터 체인을 모두 해제한 뒤 다시 할당해서 쓴다. */
int fat32_write_file(const char *name, const void *data, uint32_t size);

/* 파일을 삭제한다(클러스터 체인 해제 + 디렉토리 엔트리 0xE5). */
int fat32_remove(const char *name);

/* ── 디렉토리 조작 ───────────────────────────────────────────────────────── */

/* 빈 디렉토리 생성. 내부에 . 과 .. 엔트리를 자동으로 만든다. */
int fat32_mkdir(const char *name);

/* 비어있는 디렉토리 삭제(. / .. 외에 다른 엔트리가 있으면 실패).
   파일에는 사용 불가 — 파일은 fat32_remove. */
int fat32_rmdir(const char *name);

/* ── 작업 디렉토리(cwd) ──────────────────────────────────────────────────── */

/* 현재 디렉토리의 시작 클러스터 (마운트 시 root_cluster) */
uint32_t fat32_cwd_cluster(void);

/* cwd 변경. 한 컴포넌트(슬래시 없음)만 받는다:
     "/"      → 루트
     ".."     → 부모 ( cwd 가 이미 루트면 그대로 )
     "<dir>"  → 현재 디렉토리의 하위 디렉토리로 진입
   대상이 디렉토리가 아니면 -1. 성공 0. */
int  fat32_chdir(const char *name);

/* path 의 모든 토큰("/a/b/c" 또는 "a/b") 을 차례로 chdir.
   '/'로 시작하면 root 부터, 아니면 현재 cwd 부터.
   도중 실패 시 cwd 가 부분 이동된 채로 -1 반환 (호출자가 복원해야 함). */
int  fat32_chdir_path(const char *path);

/* cwd 를 cluster 번호로 직접 설정 (저장/복원용 저수준 API).
   0 또는 1 을 주면 root 로 보정. 검증 안 하므로 외부에서 신중히. */
void fat32_set_cwd_cluster(uint32_t cluster);
