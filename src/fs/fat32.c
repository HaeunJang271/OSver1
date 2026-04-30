#include "fat32.h"
#include "../drivers/ata.h"

/* ── BPB (FAT32 Boot Sector) 핵심 필드만 ──────────────────────────────────── */
typedef struct {
    uint8_t  jump[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;     /* offset 0x0B */
    uint8_t  sectors_per_cluster;  /* offset 0x0D */
    uint16_t reserved_sectors;     /* offset 0x0E */
    uint8_t  num_fats;             /* offset 0x10 */
    uint16_t root_entries_16;      /* FAT32=0    */
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;          /* FAT32=0    */
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* FAT32 확장 BPB (offset 0x24~) */
    uint32_t fat_size_32;          /* offset 0x24 */
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;         /* offset 0x2C */
} __attribute__((packed)) bpb32_t;

/* 한 클러스터의 최대 크기 — 64KB까지 허용 (sectors_per_cluster<=128) */
#define FAT32_CLUSTER_BUF_MAX (128 * 512)

/* ── 마운트 상태 ──────────────────────────────────────────────────────────── */
static struct {
    uint8_t  drive;
    uint8_t  sectors_per_cluster;
    uint8_t  num_fats;            /* 보통 2 (FAT 사본 개수) */
    uint16_t bytes_per_sector;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t fat_size;
    uint32_t total_clusters;      /* 데이터 영역의 총 클러스터 수 */
    uint32_t root_cluster;
    uint32_t cwd_cluster;         /* 현재 작업 디렉토리 시작 클러스터 */
    uint32_t alloc_hint;          /* 다음 alloc 시작 위치 캐시 */
    int      mounted;
} fs;

/* 한 클러스터를 통째로 담을 버퍼 (정적, 첫 4MB BSS 안에 위치) */
static uint8_t cluster_buf[FAT32_CLUSTER_BUF_MAX];
/* FAT 엔트리 조회용 1섹터 캐시 */
static uint8_t fat_sector_buf[512];
static uint32_t fat_sector_cached_lba = 0xFFFFFFFFu;

/* ── 작은 문자열 유틸 ─────────────────────────────────────────────────────── */
static char to_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static int str_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (to_upper(*a) != to_upper(*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* "FOO     TXT" → "FOO.TXT" (대문자, 끝의 공백 제거, 확장자 없으면 점 생략) */
static void format_short_name(const uint8_t raw[11], char out[13]) {
    int n = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++) out[n++] = (char)raw[i];
    if (raw[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && raw[i] != ' '; i++) out[n++] = (char)raw[i];
    }
    out[n] = '\0';
}

/* ── 클러스터 ↔ LBA 변환 / FAT 체인 추적 ──────────────────────────────────── */

static uint32_t cluster_to_lba(uint32_t cluster) {
    return fs.data_start_lba + (cluster - 2) * fs.sectors_per_cluster;
}

static int is_eoc(uint32_t cluster) {
    /* EOC: 0x0FFFFFF8 이상 (하위 28비트 기준) */
    return (cluster & 0x0FFFFFFFu) >= 0x0FFFFFF8u;
}

static uint32_t next_cluster(uint32_t cluster) {
    uint32_t fat_byte_offset = cluster * 4;
    uint32_t fat_lba = fs.fat_start_lba + fat_byte_offset / 512;
    uint32_t in_sector_off = fat_byte_offset % 512;

    if (fat_lba != fat_sector_cached_lba) {
        if (ata_read(fs.drive, fat_lba, 1, fat_sector_buf) < 0)
            return 0x0FFFFFFFu;
        fat_sector_cached_lba = fat_lba;
    }
    uint32_t v = *(uint32_t *)(fat_sector_buf + in_sector_off);
    return v & 0x0FFFFFFFu;
}

static int read_cluster(uint32_t cluster) {
    uint32_t lba = cluster_to_lba(cluster);
    return ata_read(fs.drive, lba, fs.sectors_per_cluster, cluster_buf);
}

/* ── 공개 API ─────────────────────────────────────────────────────────────── */

int fat32_mount(uint8_t drive) {
    uint8_t boot[512];
    if (ata_read(drive, 0, 1, boot) < 0) return -1;

    bpb32_t *bpb = (bpb32_t *)boot;
    if (bpb->bytes_per_sector != 512) return -1;
    if (bpb->sectors_per_cluster == 0)        return -1;
    if (bpb->fat_size_32 == 0)                return -1;     /* FAT32 아님 */
    if ((uint32_t)bpb->sectors_per_cluster * 512 > FAT32_CLUSTER_BUF_MAX)
        return -1;                                            /* 너무 큰 cluster */

    fs.drive               = drive;
    fs.bytes_per_sector    = bpb->bytes_per_sector;
    fs.sectors_per_cluster = bpb->sectors_per_cluster;
    fs.num_fats            = bpb->num_fats;
    fs.fat_start_lba       = bpb->reserved_sectors;
    fs.fat_size            = bpb->fat_size_32;
    fs.data_start_lba      = fs.fat_start_lba + fs.num_fats * fs.fat_size;
    fs.root_cluster        = bpb->root_cluster;
    fs.cwd_cluster         = fs.root_cluster;
    fs.mounted             = 1;
    fs.alloc_hint          = 2;
    fat_sector_cached_lba  = 0xFFFFFFFFu;

    /* 데이터 영역의 클러스터 총개수 = (total_sec - data_start) / spc */
    uint32_t total_sec = bpb->total_sectors_32
                       ? bpb->total_sectors_32
                       : bpb->total_sectors_16;
    fs.total_clusters = (total_sec - fs.data_start_lba) / fs.sectors_per_cluster;
    return 0;
}

int fat32_is_mounted(void)        { return fs.mounted; }
uint32_t fat32_root_cluster(void) { return fs.root_cluster; }

void fat32_listdir(uint32_t start_cluster, fat32_visitor_t visit, void *ctx) {
    if (!fs.mounted) return;

    uint32_t cluster = start_cluster;
    while (!is_eoc(cluster) && cluster >= 2) {
        if (read_cluster(cluster) < 0) return;

        uint32_t bytes = (uint32_t)fs.sectors_per_cluster * 512;
        fat32_dirent_t *e = (fat32_dirent_t *)cluster_buf;
        uint32_t n = bytes / sizeof(fat32_dirent_t);

        for (uint32_t i = 0; i < n; i++, e++) {
            uint8_t first = e->name[0];
            if (first == 0x00) return;            /* 디렉토리 끝 */
            if (first == 0xE5) continue;          /* 삭제됨        */
            if (e->attr == FAT_ATTR_LFN) continue;/* LFN 보조 엔트리 */
            if (e->attr & FAT_ATTR_VOLUME_ID) continue;
            visit(e, ctx);
        }
        cluster = next_cluster(cluster);
    }
}

/* ── 루트에서 이름 검색 ──────────────────────────────────────────────────── */
typedef struct {
    const char *target;
    int found;
    fat32_dirent_t out;
} find_ctx_t;

static void find_visitor(const fat32_dirent_t *e, void *vctx) {
    find_ctx_t *ctx = (find_ctx_t *)vctx;
    if (ctx->found) return;
    char name[13];
    format_short_name(e->name, name);
    if (str_eq_ci(name, ctx->target)) {
        ctx->out   = *e;
        ctx->found = 1;
    }
}

int fat32_find(const char *name83, fat32_dirent_t *out) {
    if (!fs.mounted) return -1;
    find_ctx_t ctx = { name83, 0, {0} };
    fat32_listdir(fs.cwd_cluster, find_visitor, &ctx);
    if (!ctx.found) return -1;
    *out = ctx.out;
    return 0;
}

int fat32_find_in_root(const char *name83, fat32_dirent_t *out) {
    if (!fs.mounted) return -1;
    find_ctx_t ctx = { name83, 0, {0} };
    fat32_listdir(fs.root_cluster, find_visitor, &ctx);
    if (!ctx.found) return -1;
    *out = ctx.out;
    return 0;
}

/* ── 파일 읽기 ───────────────────────────────────────────────────────────── */
int fat32_read_file(const fat32_dirent_t *e, void *buf, uint32_t max) {
    if (!fs.mounted) return -1;

    uint32_t cluster = ((uint32_t)e->cluster_hi << 16) | e->cluster_lo;
    uint32_t size    = e->size;
    if (size > max) size = max;

    uint8_t *dst    = (uint8_t *)buf;
    uint32_t copied = 0;

    while (!is_eoc(cluster) && cluster >= 2 && copied < size) {
        if (read_cluster(cluster) < 0) return -1;

        uint32_t bytes  = (uint32_t)fs.sectors_per_cluster * 512;
        uint32_t remain = size - copied;
        uint32_t n      = (remain < bytes) ? remain : bytes;

        for (uint32_t i = 0; i < n; i++) dst[copied + i] = cluster_buf[i];
        copied += n;
        cluster = next_cluster(cluster);
    }
    return (int)copied;
}

/* ═════════════════════════════════════════════════════════════════════════
   쓰기 지원
   ═════════════════════════════════════════════════════════════════════════ */

/* "hello.txt" → "HELLO   TXT" (대문자, 공백 패딩, 11바이트) */
static void to_short_name(const char *name, uint8_t out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, o = 0;
    while (name[i] && name[i] != '.' && o < 8) {
        out[o++] = (uint8_t)to_upper(name[i]);
        i++;
    }
    /* 점까지 base 부분 스킵 */
    while (name[i] && name[i] != '.') i++;
    if (name[i] == '.') {
        i++;
        int e = 8;
        while (name[i] && e < 11) {
            out[e++] = (uint8_t)to_upper(name[i]);
            i++;
        }
    }
}

/* FAT 사본 모두에 한 엔트리 기록 (하위 28비트만 의미) */
static int set_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_byte_off = cluster * 4;
    uint32_t in_sec_off   = fat_byte_off % 512;

    for (uint8_t f = 0; f < fs.num_fats; f++) {
        uint32_t fat_lba = fs.fat_start_lba
                         + (uint32_t)f * fs.fat_size
                         + fat_byte_off / 512;
        uint8_t buf[512];
        if (ata_read(fs.drive, fat_lba, 1, buf) < 0) return -1;
        uint32_t *e = (uint32_t *)(buf + in_sec_off);
        /* 상위 4비트는 reserved 이므로 보존 */
        *e = (*e & 0xF0000000u) | (value & 0x0FFFFFFFu);
        if (ata_write(fs.drive, fat_lba, 1, buf) < 0) return -1;
    }
    fat_sector_cached_lba = 0xFFFFFFFFu;   /* read-cache 무효화 */
    return 0;
}

/* 빈 클러스터 1개를 찾아 EOC 로 표시하고 클러스터 번호 반환. 실패 0. */
static uint32_t alloc_cluster(void) {
    if (fs.total_clusters == 0) return 0;
    uint32_t total = fs.total_clusters + 2;          /* 인덱스 [2, total) */
    if (fs.alloc_hint < 2) fs.alloc_hint = 2;

    for (uint32_t i = 0; i < fs.total_clusters; i++) {
        uint32_t c = fs.alloc_hint + i;
        if (c >= total) c = 2 + (c - total);
        if (next_cluster(c) == 0) {
            if (set_fat_entry(c, 0x0FFFFFFFu) < 0) return 0;
            fs.alloc_hint = c + 1;
            return c;
        }
    }
    return 0;       /* 디스크 가득 */
}

/* 시작 클러스터부터 체인 따라가며 모두 해제 */
static void free_chain(uint32_t start) {
    uint32_t c = start;
    while (c >= 2 && !is_eoc(c)) {
        uint32_t n = next_cluster(c);
        set_fat_entry(c, 0);
        c = n;
    }
}

/* ── 디렉토리 엔트리 위치 추적 ───────────────────────────────────────────── */
typedef struct {
    fat32_dirent_t entry;
    uint32_t lba;       /* 엔트리가 들어있는 섹터 */
    uint16_t offset;    /* 그 섹터 내 오프셋 (0, 32, 64, ...) */
    int      found;
} dir_lookup_t;

/* 11바이트 short-name 비교 (정확히 같아야 함) */
static int sname_eq(const uint8_t a[11], const uint8_t b[11]) {
    for (int i = 0; i < 11; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* 지정된 시작 클러스터의 디렉토리에서 short-name 으로 검색.
   찾으면 1, 못 찾으면 0. */
static int search_dir(uint32_t start_cluster,
                      const uint8_t target[11], dir_lookup_t *out) {
    uint32_t cluster = start_cluster;
    uint8_t  sec[512];

    while (!is_eoc(cluster) && cluster >= 2) {
        uint32_t lba_base = cluster_to_lba(cluster);
        for (uint8_t s = 0; s < fs.sectors_per_cluster; s++) {
            if (ata_read(fs.drive, lba_base + s, 1, sec) < 0) return 0;
            for (int i = 0; i < 16; i++) {
                fat32_dirent_t *e = (fat32_dirent_t *)(sec + i * 32);
                if (e->name[0] == 0x00) return 0;       /* 디렉토리 끝 */
                if (e->name[0] == 0xE5) continue;
                if (e->attr == FAT_ATTR_LFN) continue;
                if (e->attr & FAT_ATTR_VOLUME_ID) continue;
                if (sname_eq(e->name, target)) {
                    out->entry  = *e;
                    out->lba    = lba_base + s;
                    out->offset = (uint16_t)(i * 32);
                    out->found  = 1;
                    return 1;
                }
            }
        }
        cluster = next_cluster(cluster);
    }
    return 0;
}

/* 지정된 디렉토리에서 비어있는(0x00 또는 0xE5) 슬롯 찾기.
   클러스터 체인 확장은 하지 않음 — 가득 차면 -1. */
static int find_free_dirent_in(uint32_t start_cluster, dir_lookup_t *out) {
    uint32_t cluster = start_cluster;
    uint8_t  sec[512];

    while (!is_eoc(cluster) && cluster >= 2) {
        uint32_t lba_base = cluster_to_lba(cluster);
        for (uint8_t s = 0; s < fs.sectors_per_cluster; s++) {
            if (ata_read(fs.drive, lba_base + s, 1, sec) < 0) return -1;
            for (int i = 0; i < 16; i++) {
                uint8_t first = sec[i * 32];
                if (first == 0x00 || first == 0xE5) {
                    out->lba    = lba_base + s;
                    out->offset = (uint16_t)(i * 32);
                    out->found  = 0;
                    return 0;
                }
            }
        }
        cluster = next_cluster(cluster);
    }
    return -1;
}

/* dir_lookup_t 위치에 dirent 한 개를 기록 */
static int write_dirent(uint32_t lba, uint16_t offset, const fat32_dirent_t *e) {
    uint8_t sec[512];
    if (ata_read(fs.drive, lba, 1, sec) < 0) return -1;
    fat32_dirent_t *dst = (fat32_dirent_t *)(sec + offset);
    *dst = *e;
    return ata_write(fs.drive, lba, 1, sec);
}

/* ── 공개 쓰기 API ───────────────────────────────────────────────────────── */

int fat32_create(const char *name) {
    if (!fs.mounted) return -1;

    uint8_t target[11];
    to_short_name(name, target);

    dir_lookup_t look;
    if (search_dir(fs.cwd_cluster, target, &look)) return -1;     /* 이미 존재 */
    if (find_free_dirent_in(fs.cwd_cluster, &look) < 0) return -1;

    fat32_dirent_t e;
    for (uint32_t i = 0; i < sizeof(e); i++) ((uint8_t *)&e)[i] = 0;
    for (int i = 0; i < 11; i++) e.name[i] = target[i];
    e.attr       = 0x20;          /* archive (일반 파일) */
    e.cluster_lo = 0;
    e.cluster_hi = 0;
    e.size       = 0;
    return write_dirent(look.lba, look.offset, &e);
}

int fat32_remove(const char *name) {
    if (!fs.mounted) return -1;

    uint8_t target[11];
    to_short_name(name, target);

    dir_lookup_t look = {0};
    if (!search_dir(fs.cwd_cluster, target, &look)) return -1;

    /* 디렉토리는 fat32_remove 로 못 지운다 — fat32_rmdir 사용해야 함 */
    if (look.entry.attr & FAT_ATTR_DIRECTORY) return -1;

    uint32_t start = ((uint32_t)look.entry.cluster_hi << 16) | look.entry.cluster_lo;
    if (start >= 2) free_chain(start);

    uint8_t sec[512];
    if (ata_read(fs.drive, look.lba, 1, sec) < 0) return -1;
    sec[look.offset] = 0xE5;        /* 삭제 표식 */
    return ata_write(fs.drive, look.lba, 1, sec);
}

int fat32_write_file(const char *name, const void *data, uint32_t size) {
    if (!fs.mounted) return -1;

    uint8_t target[11];
    to_short_name(name, target);

    /* 1) 기존 dirent 찾기. 없으면 새로 만들 위치 확보. */
    dir_lookup_t look = {0};
    int exists = search_dir(fs.cwd_cluster, target, &look);
    if (exists && (look.entry.attr & FAT_ATTR_DIRECTORY)) return -1;

    if (!exists) {
        if (find_free_dirent_in(fs.cwd_cluster, &look) < 0) return -1;
        for (uint32_t i = 0; i < sizeof(look.entry); i++)
            ((uint8_t *)&look.entry)[i] = 0;
        for (int i = 0; i < 11; i++) look.entry.name[i] = target[i];
        look.entry.attr = 0x20;
    } else {
        uint32_t old_start = ((uint32_t)look.entry.cluster_hi << 16) | look.entry.cluster_lo;
        if (old_start >= 2) free_chain(old_start);
        look.entry.cluster_lo = 0;
        look.entry.cluster_hi = 0;
        look.entry.size       = 0;
    }

    /* 2) 클러스터 할당 + 데이터 쓰기 (체인 만들기) */
    uint32_t cluster_size = (uint32_t)fs.sectors_per_cluster * 512;
    uint32_t first = 0, prev = 0;
    uint32_t written = 0;

    while (written < size) {
        uint32_t c = alloc_cluster();
        if (c == 0) {
            if (first) free_chain(first);
            return -1;
        }
        if (!first) first = c;
        if (prev) {
            /* 직전 클러스터를 EOC 에서 c 로 갱신 */
            if (set_fat_entry(prev, c) < 0) {
                free_chain(first);
                return -1;
            }
        }
        prev = c;

        /* 한 클러스터 분량 데이터 준비 (남는 영역은 0 패딩) */
        uint8_t cbuf[FAT32_CLUSTER_BUF_MAX];
        uint32_t remain = size - written;
        uint32_t copy   = (remain < cluster_size) ? remain : cluster_size;
        for (uint32_t i = 0; i < copy; i++)
            cbuf[i] = ((const uint8_t *)data)[written + i];
        for (uint32_t i = copy; i < cluster_size; i++) cbuf[i] = 0;

        if (ata_write(fs.drive, cluster_to_lba(c),
                      fs.sectors_per_cluster, cbuf) < 0) {
            free_chain(first);
            return -1;
        }
        written += copy;
    }

    /* 빈 파일도 처리 (size==0): cluster 없이 size 만 0 */
    look.entry.cluster_lo = first & 0xFFFF;
    look.entry.cluster_hi = (first >> 16) & 0xFFFF;
    look.entry.size       = size;

    return write_dirent(look.lba, look.offset, &look.entry);
}

/* ── 디렉토리 생성 / 삭제 / 이동 ─────────────────────────────────────────── */

uint32_t fat32_cwd_cluster(void) { return fs.cwd_cluster; }

/* 현재 디렉토리 안에 빈 디렉토리 만들기 (. / .. 자동 생성) */
int fat32_mkdir(const char *name) {
    if (!fs.mounted) return -1;

    uint8_t target[11];
    to_short_name(name, target);

    /* 중복 체크 */
    dir_lookup_t look = {0};
    if (search_dir(fs.cwd_cluster, target, &look)) return -1;

    /* 부모 디렉토리에서 빈 슬롯 확보 */
    if (find_free_dirent_in(fs.cwd_cluster, &look) < 0) return -1;

    /* 1) 새 디렉토리용 클러스터 1개 할당 */
    uint32_t new_cluster = alloc_cluster();
    if (new_cluster == 0) return -1;

    /* 2) 그 클러스터를 0으로 채우고 첫 두 엔트리 . / .. 작성 */
    uint32_t cluster_size = (uint32_t)fs.sectors_per_cluster * 512;
    uint8_t  cbuf[FAT32_CLUSTER_BUF_MAX];
    for (uint32_t i = 0; i < cluster_size; i++) cbuf[i] = 0;

    fat32_dirent_t *dot    = (fat32_dirent_t *)(cbuf + 0);
    fat32_dirent_t *dotdot = (fat32_dirent_t *)(cbuf + 32);

    /* "." entry — 자기 자신 가리키기 */
    for (int i = 0; i < 11; i++) dot->name[i] = ' ';
    dot->name[0]    = '.';
    dot->attr       = FAT_ATTR_DIRECTORY;
    dot->cluster_lo = new_cluster & 0xFFFF;
    dot->cluster_hi = (new_cluster >> 16) & 0xFFFF;
    dot->size       = 0;

    /* ".." entry — 부모 가리키기. 부모가 root 면 0 으로 (FAT32 규약) */
    for (int i = 0; i < 11; i++) dotdot->name[i] = ' ';
    dotdot->name[0]    = '.';
    dotdot->name[1]    = '.';
    dotdot->attr       = FAT_ATTR_DIRECTORY;
    uint32_t parent_for_dotdot =
        (fs.cwd_cluster == fs.root_cluster) ? 0 : fs.cwd_cluster;
    dotdot->cluster_lo = parent_for_dotdot & 0xFFFF;
    dotdot->cluster_hi = (parent_for_dotdot >> 16) & 0xFFFF;
    dotdot->size       = 0;

    if (ata_write(fs.drive, cluster_to_lba(new_cluster),
                  fs.sectors_per_cluster, cbuf) < 0) {
        free_chain(new_cluster);
        return -1;
    }

    /* 3) 부모 디렉토리에 이 디렉토리의 dirent 작성 */
    fat32_dirent_t e;
    for (uint32_t i = 0; i < sizeof(e); i++) ((uint8_t *)&e)[i] = 0;
    for (int i = 0; i < 11; i++) e.name[i] = target[i];
    e.attr       = FAT_ATTR_DIRECTORY;
    e.cluster_lo = new_cluster & 0xFFFF;
    e.cluster_hi = (new_cluster >> 16) & 0xFFFF;
    e.size       = 0;

    if (write_dirent(look.lba, look.offset, &e) < 0) {
        free_chain(new_cluster);
        return -1;
    }
    return 0;
}

/* 디렉토리가 비어있는지 확인. . / .. 이외에 살아있는 엔트리가 있으면 0. */
static int dir_is_empty(uint32_t start_cluster) {
    uint32_t cluster = start_cluster;
    uint8_t  sec[512];

    while (!is_eoc(cluster) && cluster >= 2) {
        uint32_t lba_base = cluster_to_lba(cluster);
        for (uint8_t s = 0; s < fs.sectors_per_cluster; s++) {
            if (ata_read(fs.drive, lba_base + s, 1, sec) < 0) return 0;
            for (int i = 0; i < 16; i++) {
                fat32_dirent_t *e = (fat32_dirent_t *)(sec + i * 32);
                if (e->name[0] == 0x00) return 1;          /* 더 이상 엔트리 없음 */
                if (e->name[0] == 0xE5) continue;
                if (e->attr == FAT_ATTR_LFN) continue;
                if (e->attr & FAT_ATTR_VOLUME_ID) continue;
                /* "." 또는 ".." 는 무시 */
                if (e->name[0] == '.' &&
                    (e->name[1] == ' ' || e->name[1] == '.')) continue;
                return 0;       /* 다른 엔트리 발견 → 비어있지 않음 */
            }
        }
        cluster = next_cluster(cluster);
    }
    return 1;
}

int fat32_rmdir(const char *name) {
    if (!fs.mounted) return -1;

    /* . / .. 는 못 지움 */
    if (name[0] == '.' && (name[1] == '\0' ||
        (name[1] == '.' && name[2] == '\0'))) return -1;

    uint8_t target[11];
    to_short_name(name, target);

    dir_lookup_t look = {0};
    if (!search_dir(fs.cwd_cluster, target, &look)) return -1;
    if (!(look.entry.attr & FAT_ATTR_DIRECTORY)) return -1;  /* 디렉토리 아님 */

    uint32_t target_cluster =
        ((uint32_t)look.entry.cluster_hi << 16) | look.entry.cluster_lo;
    if (target_cluster < 2) return -1;
    if (!dir_is_empty(target_cluster)) return -1;

    /* 클러스터 체인 해제 + dirent 삭제 표식 */
    free_chain(target_cluster);

    uint8_t sec[512];
    if (ata_read(fs.drive, look.lba, 1, sec) < 0) return -1;
    sec[look.offset] = 0xE5;
    return ata_write(fs.drive, look.lba, 1, sec);
}

int fat32_chdir(const char *name) {
    if (!fs.mounted) return -1;

    /* 특수 케이스: 루트 */
    if (name[0] == '/' && name[1] == '\0') {
        fs.cwd_cluster = fs.root_cluster;
        return 0;
    }

    /* 특수 케이스: . — 변화 없음 */
    if (name[0] == '.' && name[1] == '\0') return 0;

    /* 특수 케이스: .. — 부모로 (이미 root 면 그대로) */
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        if (fs.cwd_cluster == fs.root_cluster) return 0;

        uint8_t dotdot[11] = { '.', '.', ' ',' ',' ',' ',' ',' ',' ',' ',' ' };
        dir_lookup_t look = {0};
        if (!search_dir(fs.cwd_cluster, dotdot, &look)) return -1;

        uint32_t parent =
            ((uint32_t)look.entry.cluster_hi << 16) | look.entry.cluster_lo;
        /* FAT32 규약: 부모가 root 면 .. 의 cluster 가 0 으로 기록됨 */
        if (parent == 0) parent = fs.root_cluster;
        fs.cwd_cluster = parent;
        return 0;
    }

    /* 일반 디렉토리 진입 */
    uint8_t target[11];
    to_short_name(name, target);
    dir_lookup_t look = {0};
    if (!search_dir(fs.cwd_cluster, target, &look)) return -1;
    if (!(look.entry.attr & FAT_ATTR_DIRECTORY)) return -1;

    uint32_t c = ((uint32_t)look.entry.cluster_hi << 16) | look.entry.cluster_lo;
    if (c == 0) c = fs.root_cluster;       /* .. 이 0 인 경우와 동일 처리 */
    fs.cwd_cluster = c;
    return 0;
}

/* 토큰별 chdir. '/' 로 시작하면 root 에서 출발. 빈 토큰("//") 은 스킵. */
int fat32_chdir_path(const char *path) {
    if (!fs.mounted) return -1;

    const char *p = path;
    if (p[0] == '/') {
        fs.cwd_cluster = fs.root_cluster;
        p++;
    }

    char tok[16];
    int  ti = 0;
    while (1) {
        if (*p == '/' || *p == '\0') {
            if (ti > 0) {
                tok[ti] = '\0';
                if (fat32_chdir(tok) < 0) return -1;
                ti = 0;
            }
            if (*p == '\0') return 0;
            p++;
        } else {
            if (ti < 15) tok[ti++] = *p;
            p++;
        }
    }
}

void fat32_set_cwd_cluster(uint32_t cluster) {
    if (!fs.mounted) return;
    if (cluster < 2) cluster = fs.root_cluster;
    fs.cwd_cluster = cluster;
}
