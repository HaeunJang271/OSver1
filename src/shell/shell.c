#include "shell.h"
#include "../drivers/screen.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../mem/pmm.h"
#include "../mem/paging.h"
#include "../mem/kheap.h"
#include "../fs/fat32.h"
#include "../proc/task.h"
#include "../proc/elf.h"

#define LINE_MAX  256
#define ARGS_MAX  16

/* ── 문자열 유틸 ─────────────────────────────────────────────────────────── */

static int str_len(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

/* "0x1A2B" 또는 "1234" → uint32_t */
static uint32_t parse_uint(const char *s) {
    uint32_t v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        for (; *s; s++) {
            v <<= 4;
            if (*s >= '0' && *s <= '9') v |= (uint32_t)(*s - '0');
            else if (*s >= 'a' && *s <= 'f') v |= (uint32_t)(*s - 'a' + 10);
            else if (*s >= 'A' && *s <= 'F') v |= (uint32_t)(*s - 'A' + 10);
            else break;
        }
    } else {
        for (; *s >= '0' && *s <= '9'; s++)
            v = v * 10 + (uint32_t)(*s - '0');
    }
    return v;
}

/* ── 명령어 파서 ─────────────────────────────────────────────────────────── */
/* line을 공백 기준으로 나눠 argv에 채우고 argc 반환 (line 내용 변경됨) */

static int parse(char *line, char *argv[]) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        if (argc == ARGS_MAX) break;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

/* ── 내장 명령어 ─────────────────────────────────────────────────────────── */

typedef struct {
    const char *name;
    const char *desc;
    void (*fn)(int argc, char **argv);
} cmd_t;

static void cmd_help(int, char **);
static void cmd_clear(int, char **);
static void cmd_echo(int, char **);
static void cmd_meminfo(int, char **);
static void cmd_page(int, char **);
static void cmd_vmap(int, char **);
static void cmd_pftest(int, char **);
static void cmd_ls(int, char **);
static void cmd_cat(int, char **);
static void cmd_touch(int, char **);
static void cmd_write(int, char **);
static void cmd_rm(int, char **);
static void cmd_mkdir(int, char **);
static void cmd_rmdir(int, char **);
static void cmd_cd(int, char **);
static void cmd_pwd(int, char **);
static void cmd_uptime(int, char **);
static void cmd_sleep(int, char **);
static void cmd_heap(int, char **);
static void cmd_kalloc(int, char **);
static void cmd_tasks(int, char **);
static void cmd_spawn(int, char **);
static void cmd_yield(int, char **);
static void cmd_user(int, char **);
static void cmd_exec(int, char **);
static void cmd_version(int, char **);
static void cmd_halt(int, char **);

/* user-mode 데모 entry — src/proc/user_demo.c */
extern void user_demo_entry(void);

static const cmd_t cmds[] = {
    { "help",    "show this command list",                       cmd_help    },
    { "clear",   "clear the screen",                             cmd_clear   },
    { "echo",    "print text                echo <text>",        cmd_echo    },
    { "meminfo", "show memory usage",                            cmd_meminfo },
    { "page",    "alloc / free a page      page alloc|free <addr>", cmd_page },
    { "vmap",    "show v->p mapping        vmap <virt>",         cmd_vmap    },
    { "pf-test", "trigger a page fault",                         cmd_pftest  },
    { "ls",      "list files               ls [path]",           cmd_ls      },
    { "cat",     "print a file             cat <path>",          cmd_cat     },
    { "touch",   "create empty file        touch <path>",        cmd_touch   },
    { "write",   "overwrite file content   write <path> <text>", cmd_write   },
    { "rm",      "delete a file            rm <path>",           cmd_rm      },
    { "mkdir",   "create a directory       mkdir <path>",        cmd_mkdir   },
    { "rmdir",   "remove an empty dir      rmdir <path>",        cmd_rmdir   },
    { "cd",      "change directory         cd <path> | .. | /",  cmd_cd      },
    { "pwd",     "print current path",                           cmd_pwd     },
    { "uptime",  "show how long the system has been up",         cmd_uptime  },
    { "sleep",   "wait N seconds            sleep <seconds>",    cmd_sleep   },
    { "heap",    "show kernel heap stats",                       cmd_heap    },
    { "kalloc",  "kmalloc/kfree test        kalloc alloc <n> | kalloc free <addr>", cmd_kalloc },
    { "tasks",   "list scheduled tasks",                         cmd_tasks   },
    { "spawn",   "spawn N counter tasks     spawn <count>",      cmd_spawn   },
    { "yield",   "voluntarily give up CPU",                      cmd_yield   },
    { "user",    "run a ring-3 demo task",                       cmd_user    },
    { "exec",    "load an ELF and run it      exec <path>",      cmd_exec    },
    { "version", "show OS version info",                         cmd_version },
    { "halt",    "halt the system",                              cmd_halt    },
};
#define NUM_CMDS ((int)(sizeof(cmds) / sizeof(cmds[0])))

/* help */
static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    kprint_color("Commands:\n", VGA_LIGHT_CYAN, VGA_BLACK);
    for (int i = 0; i < NUM_CMDS; i++) {
        kprint("  ");
        kprint_color(cmds[i].name, VGA_YELLOW, VGA_BLACK);
        int pad = 10 - str_len(cmds[i].name);
        for (int j = 0; j < pad; j++) kputchar(' ');
        kprint(cmds[i].desc);
        kputchar('\n');
    }
}

/* clear */
static void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    clear_screen();
}

/* echo */
static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) kputchar(' ');
        kprint(argv[i]);
    }
    kputchar('\n');
}

/* meminfo */
static void cmd_meminfo(int argc, char **argv) {
    (void)argc; (void)argv;

    uint32_t total_kb = pmm_total_pages() * (PAGE_SIZE / 1024);
    uint32_t free_kb  = pmm_free_pages()  * (PAGE_SIZE / 1024);
    uint32_t used_kb  = total_kb - free_kb;

    kprint_color("Physical Memory\n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint("  Total : "); kprint_dec(total_kb / 1024); kprint(" MB\n");
    kprint("  Used  : "); kprint_dec(used_kb  / 1024); kprint(" MB\n");
    kprint("  Free  : "); kprint_dec(free_kb  / 1024); kprint(" MB\n");

    /* 막대 그래프 */
    if (total_kb == 0) return;
    kprint("  [");
    int used_bars = (int)(used_kb * 30 / total_kb);
    for (int i = 0; i < 30; i++) {
        if (i < used_bars) kprint_color("|", VGA_LIGHT_RED,   VGA_BLACK);
        else               kprint_color("-", VGA_LIGHT_GREEN, VGA_BLACK);
    }
    kprint("] ");
    kprint_dec(used_kb * 100 / total_kb);
    kprint("% used\n");

    /* 힙 요약 한 줄 (자세히 보려면 'heap' 명령) */
    kheap_stats_t hs;
    kheap_get_stats(&hs);
    kprint_color("Kernel Heap : ", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint_dec(hs.used_bytes); kprint(" / ");
    kprint_dec(hs.total_bytes); kprint(" bytes used");
    kprint(" ("); kprint_dec(hs.blocks_used); kprint(" blocks)\n");
}

/* page — PMM 직접 테스트 */
static void cmd_page(int argc, char **argv) {
    if (argc < 2) {
        kprint("usage: page alloc | page free <addr>\n");
        return;
    }
    if (str_eq(argv[1], "alloc")) {
        void *p = pmm_alloc();
        if (!p) {
            kprint_color("Out of memory!\n", VGA_LIGHT_RED, VGA_BLACK);
        } else {
            kprint("Allocated page at: ");
            kprint_hex((uint32_t)p);
            kprint("\n");
        }
    } else if (str_eq(argv[1], "free") && argc >= 3) {
        uint32_t addr = parse_uint(argv[2]);
        if (addr % PAGE_SIZE != 0) {
            kprint_color("Address must be page-aligned (multiple of 4096)\n",
                         VGA_LIGHT_RED, VGA_BLACK);
            return;
        }
        pmm_free((void *)addr);
        kprint("Freed page at: ");
        kprint_hex(addr);
        kprint("\n");
    } else {
        kprint("usage: page alloc | page free <addr>\n");
    }
}

/* vmap <virt> — 가상 주소의 매핑 조회 */
static void cmd_vmap(int argc, char **argv) {
    if (argc < 2) {
        kprint("usage: vmap <virt>\n");
        return;
    }
    uint32_t v = parse_uint(argv[1]);
    uint32_t p = vmm_get_phys(v);
    kprint("virt "); kprint_hex(v);
    if (p == 0xFFFFFFFFu) {
        kprint_color(" -> (unmapped)\n", VGA_LIGHT_RED, VGA_BLACK);
    } else {
        kprint(" -> phys "); kprint_hex(p); kprint("\n");
    }
}

/* pf-test — 매핑 없는 주소 읽기로 페이지 폴트 유발 */
static void cmd_pftest(int argc, char **argv) {
    (void)argc; (void)argv;
    kprint_color("Triggering page fault at 0xDEADC000 ...\n",
                 VGA_LIGHT_RED, VGA_BLACK);
    volatile uint32_t *bad = (volatile uint32_t *)0xDEADC000u;
    volatile uint32_t  v   = *bad;     /* PF! */
    (void)v;
    kprint("(unreachable)\n");
}

/* ── FAT32 셸 명령 ─────────────────────────────────────────────────────── */

/* "FOO     TXT" → "FOO.TXT" 의 출력용 변환 (12바이트 + NUL) */
static void short_name_to_str(const uint8_t raw[11], char out[13]) {
    int n = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++) out[n++] = (char)raw[i];
    if (raw[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && raw[i] != ' '; i++) out[n++] = (char)raw[i];
    }
    out[n] = '\0';
}

static void ls_visitor(const fat32_dirent_t *e, void *ctx) {
    (void)ctx;
    char name[13];
    short_name_to_str(e->name, name);

    if (e->attr & FAT_ATTR_DIRECTORY) {
        kprint_color("  <DIR>  ", VGA_LIGHT_CYAN, VGA_BLACK);
        kprint(name);
        kprint("\n");
    } else {
        kprint("         ");
        kprint(name);
        int pad = 14 - str_len(name);
        for (int i = 0; i < pad; i++) kputchar(' ');
        kprint_dec(e->size);
        kprint(" bytes\n");
    }
}

/* ── 깊은 경로 처리 헬퍼 ─────────────────────────────────────────────────── */

/* path 를 분해해 마지막 토큰 직전까지 cwd 를 이동시키고, 마지막 토큰을
   basename 에 복사한다. 호출자는 작업 후 fat32_set_cwd_cluster(saved) 로
   원래 cwd 로 복원해야 한다.

   path 예시:
     "foo.txt"        → cwd 그대로,        basename = "foo.txt"
     "a/b/foo.txt"    → cwd → a → b,       basename = "foo.txt"
     "/a/b/foo.txt"   → cwd → root → a → b, basename = "foo.txt"
     "/" 또는 ""       → basename = ""  (호출자가 빈 검사)
   성공 0, 실패 -1 (이 경우 cwd 가 부분 이동된 상태일 수 있음). */
static int path_descend(const char *path, char basename[16]) {
    const char *p = path;
    if (p[0] == '/') {
        fat32_chdir("/");
        p++;
    }

    char prev[16];
    int  prev_set = 0;
    char cur[16];
    int  ci = 0;

    while (1) {
        if (*p == '/' || *p == '\0') {
            if (ci > 0) {
                cur[ci] = '\0';
                if (prev_set) {
                    if (fat32_chdir(prev) < 0) return -1;
                }
                for (int i = 0; i <= ci; i++) prev[i] = cur[i];
                prev_set = 1;
                ci = 0;
            }
            if (*p == '\0') break;
            p++;
        } else {
            if (ci < 15) cur[ci++] = *p;
            p++;
        }
    }

    if (prev_set) {
        for (int i = 0; ; i++) {
            basename[i] = prev[i];
            if (!prev[i]) break;
        }
    } else {
        basename[0] = '\0';
    }
    return 0;
}

/* path → 부모로 cwd 이동 + basename 추출. 실패하면 cwd 복원하고 -1. */
static int path_to_parent_and_base(const char *path,
                                   uint32_t *saved_out,
                                   char basename[16]) {
    *saved_out = fat32_cwd_cluster();
    if (path_descend(path, basename) < 0 || basename[0] == '\0') {
        fat32_set_cwd_cluster(*saved_out);
        return -1;
    }
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────── */

static void cmd_ls(int argc, char **argv) {
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    uint32_t saved = fat32_cwd_cluster();
    if (argc >= 2) {
        if (fat32_chdir_path(argv[1]) < 0) {
            fat32_set_cwd_cluster(saved);
            kprint_color("ls: not a directory or not found: ",
                         VGA_LIGHT_RED, VGA_BLACK);
            kprint(argv[1]); kprint("\n");
            return;
        }
    }
    fat32_listdir(fat32_cwd_cluster(), ls_visitor, 0);
    fat32_set_cwd_cluster(saved);
}

#define CAT_BUF_SIZE 16384      /* 최대 16KB 텍스트 표시 */

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) { kprint("usage: cat <path>\n"); return; }
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }

    uint32_t saved;
    char base[16];
    if (path_to_parent_and_base(argv[1], &saved, base) < 0) {
        kprint_color("cat: invalid path\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    fat32_dirent_t e;
    int find_rc = fat32_find(base, &e);
    fat32_set_cwd_cluster(saved);

    if (find_rc < 0) {
        kprint_color("not found: ", VGA_LIGHT_RED, VGA_BLACK);
        kprint(argv[1]); kprint("\n");
        return;
    }
    if (e.attr & FAT_ATTR_DIRECTORY) {
        kprint_color("is a directory\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    /* 매번 동적 할당 — 16KB BSS 정적 버퍼 제거. heap 부족 시 graceful fail. */
    uint8_t *buf = (uint8_t *)kmalloc(CAT_BUF_SIZE);
    if (!buf) {
        kprint_color("cat: out of heap\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    int n = fat32_read_file(&e, buf, CAT_BUF_SIZE);
    if (n < 0) {
        kfree(buf);
        kprint_color("read error\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    for (int i = 0; i < n; i++) {
        char c = (char)buf[i];
        if (c == '\r') continue;          /* CRLF → LF */
        kputchar(c);
    }
    if (n > 0 && buf[n - 1] != '\n') kputchar('\n');
    kfree(buf);
}

/* touch <path> — 빈 파일 생성 */
static void cmd_touch(int argc, char **argv) {
    if (argc < 2) { kprint("usage: touch <path>\n"); return; }
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    uint32_t saved;
    char base[16];
    if (path_to_parent_and_base(argv[1], &saved, base) < 0) {
        kprint_color("touch: invalid path\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    int r = fat32_create(base);
    fat32_set_cwd_cluster(saved);
    if (r < 0) {
        kprint_color("touch failed (exists or directory full)\n",
                     VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    kprint("created: "); kprint(argv[1]); kprint("\n");
}

/* write <path> <text...> — argv[2..] 를 공백으로 합쳐 파일에 덮어쓰기 */
#define WRITE_BUF_SIZE 8192

static void cmd_write(int argc, char **argv) {
    if (argc < 3) { kprint("usage: write <path> <text...>\n"); return; }
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }

    uint8_t *buf = (uint8_t *)kmalloc(WRITE_BUF_SIZE);
    if (!buf) {
        kprint_color("write: out of heap\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    uint32_t len = 0;
    for (int i = 2; i < argc; i++) {
        if (i > 2 && len < WRITE_BUF_SIZE) buf[len++] = ' ';
        const char *s = argv[i];
        while (*s && len < WRITE_BUF_SIZE) buf[len++] = (uint8_t)*s++;
    }
    if (len < WRITE_BUF_SIZE) buf[len++] = '\n';

    uint32_t saved;
    char base[16];
    if (path_to_parent_and_base(argv[1], &saved, base) < 0) {
        kfree(buf);
        kprint_color("write: invalid path\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    int r = fat32_write_file(base, buf, len);
    fat32_set_cwd_cluster(saved);
    kfree(buf);

    if (r < 0) {
        kprint_color("write failed (disk or directory full?)\n",
                     VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    kprint("wrote "); kprint_dec(len);
    kprint(" bytes to "); kprint(argv[1]); kprint("\n");
}

/* rm <path> — 파일 삭제 */
static void cmd_rm(int argc, char **argv) {
    if (argc < 2) { kprint("usage: rm <path>\n"); return; }
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    uint32_t saved;
    char base[16];
    if (path_to_parent_and_base(argv[1], &saved, base) < 0) {
        kprint_color("rm: invalid path\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    int r = fat32_remove(base);
    fat32_set_cwd_cluster(saved);
    if (r < 0) {
        kprint_color("rm failed (not found, or it's a directory?)\n",
                     VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    kprint("removed: "); kprint(argv[1]); kprint("\n");
}

/* ── 디렉토리 + cwd 추적 ────────────────────────────────────────────────── */

#define CWD_PATH_MAX 128
static char cwd_path[CWD_PATH_MAX] = "/";

/* 8.3 short name 을 셸 표기용으로 정리 ("FOO     TXT" → "FOO.TXT") */
static void short_to_display(const char *src, char dst[13]) {
    /* 입력은 사용자가 친 그대로(소문자 가능) — 우선 대문자로 변환 */
    int n = 0;
    int dot_seen = 0;
    for (int i = 0; src[i] && n < 12; i++) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == '.') dot_seen = 1;
        dst[n++] = c;
    }
    (void)dot_seen;
    dst[n] = '\0';
}

static void path_push(const char *name) {
    char up[13];
    short_to_display(name, up);

    int len = str_len(cwd_path);
    int add = str_len(up);
    /* 슬래시 1개 + 이름 + NUL */
    if (len + 1 + add + 1 >= CWD_PATH_MAX) return;
    if (!(len == 1 && cwd_path[0] == '/')) {
        cwd_path[len++] = '/';
    }
    for (int i = 0; i < add; i++) cwd_path[len++] = up[i];
    cwd_path[len] = '\0';
}

static void path_pop(void) {
    int len = str_len(cwd_path);
    if (len <= 1) { cwd_path[0] = '/'; cwd_path[1] = '\0'; return; }
    /* 뒤에서 '/' 까지 자르기 */
    while (len > 1 && cwd_path[len - 1] != '/') len--;
    if (len > 1) len--;          /* 그 슬래시도 제거 */
    cwd_path[len] = '\0';
    if (cwd_path[0] == '\0') { cwd_path[0] = '/'; cwd_path[1] = '\0'; }
}

static void cmd_mkdir(int argc, char **argv) {
    if (argc < 2) { kprint("usage: mkdir <path>\n"); return; }
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    uint32_t saved;
    char base[16];
    if (path_to_parent_and_base(argv[1], &saved, base) < 0) {
        kprint_color("mkdir: invalid path\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    int r = fat32_mkdir(base);
    fat32_set_cwd_cluster(saved);
    if (r < 0) {
        kprint_color("mkdir failed (exists, full, or out of space)\n",
                     VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    kprint("created dir: "); kprint(argv[1]); kprint("\n");
}

static void cmd_rmdir(int argc, char **argv) {
    if (argc < 2) { kprint("usage: rmdir <path>\n"); return; }
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    uint32_t saved;
    char base[16];
    if (path_to_parent_and_base(argv[1], &saved, base) < 0) {
        kprint_color("rmdir: invalid path\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    int r = fat32_rmdir(base);
    fat32_set_cwd_cluster(saved);
    if (r < 0) {
        kprint_color("rmdir failed (not a dir, or not empty)\n",
                     VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    kprint("removed dir: "); kprint(argv[1]); kprint("\n");
}

static void cmd_cd(int argc, char **argv) {
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    const char *target = (argc < 2) ? "/" : argv[1];

    /* 실패 시 복원할 cwd + path 저장 */
    uint32_t saved_cluster = fat32_cwd_cluster();
    char saved_path[CWD_PATH_MAX];
    for (int i = 0; ; i++) {
        saved_path[i] = cwd_path[i];
        if (!cwd_path[i]) break;
    }

    /* 절대 경로면 root 부터 시작 */
    const char *p = target;
    if (p[0] == '/') {
        fat32_chdir("/");
        cwd_path[0] = '/'; cwd_path[1] = '\0';
        p++;
    }

    /* 토큰별 chdir + path 갱신 */
    char tok[16];
    int  ti = 0;
    while (1) {
        if (*p == '/' || *p == '\0') {
            if (ti > 0) {
                tok[ti] = '\0';
                if (fat32_chdir(tok) < 0) {
                    /* 원자적으로 원위치 복원 */
                    fat32_set_cwd_cluster(saved_cluster);
                    for (int i = 0; ; i++) {
                        cwd_path[i] = saved_path[i];
                        if (!saved_path[i]) break;
                    }
                    kprint_color("cd failed: ", VGA_LIGHT_RED, VGA_BLACK);
                    kprint(target); kprint("\n");
                    return;
                }
                if (tok[0] == '.' && tok[1] == '\0') {
                    /* "." → 변화 없음 */
                } else if (tok[0] == '.' && tok[1] == '.' && tok[2] == '\0') {
                    path_pop();
                } else {
                    path_push(tok);
                }
                ti = 0;
            }
            if (*p == '\0') return;
            p++;
        } else {
            if (ti < 15) tok[ti++] = *p;
            p++;
        }
    }
}

static void cmd_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    kprint(cwd_path); kprint("\n");
}

/* ── 시간 / 디버그 명령 ─────────────────────────────────────────────────── */

/* uptime — 시스템이 부팅 후 흐른 시간 (시:분:초) */
static void cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t total = timer_uptime_seconds();
    uint32_t hr = total / 3600;
    uint32_t mn = (total / 60) % 60;
    uint32_t sc = total % 60;

    kprint("up ");
    kprint_dec(hr); kprint("h ");
    if (mn < 10) kputchar('0'); kprint_dec(mn); kprint("m ");
    if (sc < 10) kputchar('0'); kprint_dec(sc); kprint("s   (");
    kprint_dec(timer_ticks()); kprint(" ticks @ ");
    kprint_dec(TIMER_HZ); kprint(" Hz)\n");
}

/* sleep <seconds> */
static void cmd_sleep(int argc, char **argv) {
    if (argc < 2) { kprint("usage: sleep <seconds>\n"); return; }
    uint32_t sec = parse_uint(argv[1]);
    if (sec == 0) {
        kprint("sleep: 0 seconds, returning immediately\n");
        return;
    }
    if (sec > 3600) sec = 3600;          /* 안전 한도 — 셸이 너무 오래 멈추지 않도록 */
    timer_sleep_ms(sec * 1000u);
}

/* ── 힙 관련 명령 ────────────────────────────────────────────────────────── */

static void cmd_heap(int argc, char **argv) {
    (void)argc; (void)argv;
    kheap_stats_t s;
    kheap_get_stats(&s);

    kprint_color("Kernel Heap\n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint("  Arena : "); kprint_dec(s.total_bytes); kprint(" bytes\n");
    kprint("  Used  : "); kprint_dec(s.used_bytes ); kprint(" bytes (");
    kprint_dec(s.blocks_used); kprint(" blocks)\n");
    kprint("  Free  : "); kprint_dec(s.free_bytes ); kprint(" bytes (");
    kprint_dec(s.blocks_free); kprint(" blocks)\n");
    kprint("  Largest free block: "); kprint_dec(s.largest_free); kprint(" bytes\n");

    /* 막대 그래프 — 32칸 */
    if (s.total_bytes == 0) return;
    kprint("  [");
    /* 헤더가 차지하는 공간을 단순화하기 위해 used+free 비율로 그린다 */
    uint32_t denom = s.used_bytes + s.free_bytes;
    if (denom == 0) denom = 1;
    int used_bars = (int)(s.used_bytes * 32u / denom);
    for (int i = 0; i < 32; i++) {
        if (i < used_bars) kprint_color("|", VGA_LIGHT_RED,   VGA_BLACK);
        else               kprint_color("-", VGA_LIGHT_GREEN, VGA_BLACK);
    }
    kprint("] ");
    kprint_dec(s.used_bytes * 100u / denom);
    kprint("% used\n");
}

static void cmd_kalloc(int argc, char **argv) {
    if (argc < 2) {
        kprint("usage: kalloc alloc <bytes> | kalloc free <addr>\n");
        return;
    }
    if (str_eq(argv[1], "alloc") && argc >= 3) {
        uint32_t n = parse_uint(argv[2]);
        void *p = kmalloc(n);
        if (!p) {
            kprint_color("kmalloc returned NULL (OOM)\n",
                         VGA_LIGHT_RED, VGA_BLACK);
            return;
        }
        kprint("kmalloc("); kprint_dec(n); kprint(") -> ");
        kprint_hex((uint32_t)p); kprint("\n");
    } else if (str_eq(argv[1], "free") && argc >= 3) {
        uint32_t a = parse_uint(argv[2]);
        kfree((void *)a);
        kprint("kfree("); kprint_hex(a); kprint(") done\n");
    } else {
        kprint("usage: kalloc alloc <bytes> | kalloc free <addr>\n");
    }
}

/* ── 멀티태스킹 명령 ─────────────────────────────────────────────────── */

static const char *state_str(task_state_t s) {
    switch (s) {
        case TASK_RUNNING: return "RUN ";
        case TASK_READY:   return "RDY ";
        case TASK_ZOMBIE:  return "DEAD";
        default:           return "????";
    }
}

static void cmd_tasks(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!tasking_active()) {
        kprint("multitasking not active\n");
        return;
    }
    task_info_t info[16];
    int n = task_list_snapshot(info, 16);
    kprint_color("ID  STATE  TICKS    NAME\n", VGA_LIGHT_CYAN, VGA_BLACK);
    for (int i = 0; i < n; i++) {
        if (info[i].is_current) kprint_color("*", VGA_LIGHT_GREEN, VGA_BLACK);
        else                     kputchar(' ');
        kprint(" ");
        kprint_dec(info[i].id);
        if (info[i].id < 10) kputchar(' ');
        kprint("  ");
        kprint(state_str(info[i].state));
        kprint("   ");
        kprint_dec(info[i].ticks_run);
        int pad = 8 - 4;            /* 대략 정렬용 */
        for (int j = 0; j < pad; j++) kputchar(' ');
        kprint(info[i].name);
        kputchar('\n');
    }
}

/* spawn 으로 만들어지는 데모 카운터 task — 5초간 자기 ID 출력 후 종료 */
static void counter_task(void) {
    task_t *me = task_current();
    /* 안전: 너무 빨리 부르면 스택에 me 가 stale 일 수 있어 매번 갱신 */
    for (int i = 0; i < 5; i++) {
        kprint("\n[T"); kprint_dec(me->id);
        kprint(":"); kprint_dec(i); kprint("] tick from ");
        kprint(me->name); kputchar('\n');
        timer_sleep_ms(1000);
    }
    /* return 하면 task_start_trampoline 이 task_exit 호출 */
}

static void cmd_spawn(int argc, char **argv) {
    if (argc < 2) {
        kprint("usage: spawn <count>\n");
        return;
    }
    if (!tasking_active()) {
        kprint_color("multitasking not active\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    uint32_t n = parse_uint(argv[1]);
    if (n == 0)  n = 1;
    if (n > 8)   n = 8;

    char nm[TASK_NAME_LEN];
    for (uint32_t i = 0; i < n; i++) {
        nm[0] = 'c'; nm[1] = 'n'; nm[2] = 't';
        nm[3] = '-'; nm[4] = (char)('0' + (i % 10));
        nm[5] = '\0';
        task_t *t = task_create(nm, counter_task);
        if (!t) {
            kprint_color("spawn failed (kheap?)\n",
                         VGA_LIGHT_RED, VGA_BLACK);
            return;
        }
        kprint("spawned task #"); kprint_dec(t->id);
        kprint(" '"); kprint(t->name); kprint("'\n");
    }
}

static void cmd_yield(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!tasking_active()) {
        kprint_color("multitasking not active\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    task_yield();
    kprint("(back from yield)\n");
}

/* user — ring 3 데모 task 를 만든다.
   user_demo_entry 가 syscall 만으로 화면 출력 + sleep + exit 진행. */
static void cmd_user(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!tasking_active()) {
        kprint_color("multitasking not active\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    task_t *t = task_create_user("ring3-demo", user_demo_entry);
    if (!t) {
        kprint_color("failed to spawn user task (kheap?)\n",
                     VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    kprint("spawned ring-3 task #"); kprint_dec(t->id);
    kprint(" '"); kprint(t->name); kprint("'\n");
}

/* exec — FAT32 에서 ELF 파일을 읽어 ring 3 task 로 실행한다. */
static void cmd_exec(int argc, char **argv) {
    if (argc < 2) { kprint("usage: exec <path>\n"); return; }
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    if (!tasking_active()) {
        kprint_color("multitasking not active\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    /* 1) 파일 찾기 (path → cwd 임시 이동 + basename 검색) */
    uint32_t saved;
    char base[16];
    if (path_to_parent_and_base(argv[1], &saved, base) < 0) {
        kprint_color("exec: invalid path\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    fat32_dirent_t e;
    int rc = fat32_find(base, &e);
    fat32_set_cwd_cluster(saved);

    if (rc < 0) {
        kprint_color("exec: not found: ", VGA_LIGHT_RED, VGA_BLACK);
        kprint(argv[1]); kprint("\n");
        return;
    }
    if (e.attr & FAT_ATTR_DIRECTORY) {
        kprint_color("exec: is a directory\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    if (e.size == 0) {
        kprint_color("exec: empty file\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    /* 2) 파일 전체를 힙에 읽기 */
    uint8_t *buf = (uint8_t *)kmalloc(e.size);
    if (!buf) {
        kprint_color("exec: out of heap\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    int n = fat32_read_file(&e, buf, e.size);
    if (n < 0 || (uint32_t)n != e.size) {
        kfree(buf);
        kprint_color("exec: read error\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    /* 3) ELF 파싱 + LOAD 세그먼트 매핑 */
    user_image_t *img = (user_image_t *)0;
    int er = elf_load(buf, e.size, &img);
    kfree(buf);
    if (er < 0) {
        kprint_color("exec: not a valid i386 ELF (rc=", VGA_LIGHT_RED, VGA_BLACK);
        kprint_dec((uint32_t)(-er)); kprint(")\n");
        return;
    }

    /* 4) ring 3 task 생성 */
    task_t *t = task_create_user_image(argv[1], img);
    if (!t) {
        elf_unload(img);
        kprint_color("exec: failed to spawn task\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    kprint("exec: pid="); kprint_dec(t->id);
    kprint(" entry="); kprint_hex(img->entry);
    kprint(" segs="); kprint_dec(img->seg_count); kprint("\n");
}

/* version */
static void cmd_version(int argc, char **argv) {
    (void)argc; (void)argv;
    kprint_color("MyOS v1.0  (mini-Unix milestone)\n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint("  Arch  : x86 (i686), 32-bit protected mode + paging\n");
    kprint("  Kernel: custom bootloader + C kernel\n");
    kprint("  Phases: boot / GDT+IDT / keyboard / PMM / shell / paging\n");
    kprint("        / FAT32 / serial+PIT / kheap / multitasking\n");
    kprint("        / user mode + syscalls / ELF loader\n");
    kprint("  FS    : ls cat touch write rm mkdir rmdir cd pwd\n");
    kprint("  Time  : uptime sleep   (PIT 100 Hz, COM1 115200 8N1)\n");
    kprint("  Heap  : heap kalloc    (256 KB freelist, 8B align, coalesce)\n");
    kprint("  Tasks : tasks spawn yield user exec  (ring 0 + ring 3)\n");
    kprint("  Sys   : exit write getpid sleep_ms (4 syscalls)\n");
    kprint("  Exec  : ELF32 PT_LOAD -> 0x40000000 (vmm_map + USER pages)\n");
}

/* halt */
static void cmd_halt(int argc, char **argv) {
    (void)argc; (void)argv;
    kprint_color("System halted.\n", VGA_LIGHT_RED, VGA_BLACK);
    __asm__ volatile ("cli; hlt");
}

/* ── 한 줄 입력 ──────────────────────────────────────────────────────────── */

static void readline(char *buf, int max) {
    int len = 0;
    int pcol, prow;
    kget_cursor(&pcol, &prow);

    for (;;) {
        char c = keyboard_getchar();
        if (c == '\n') {
            kputchar('\n');
            buf[len] = '\0';
            return;
        }
        if (c == '\b') {
            if (len > 0) {
                int cc, cr;
                kget_cursor(&cc, &cr);
                if (cr > prow || (cr == prow && cc > pcol)) {
                    kputchar('\b');
                    len--;
                }
            }
        } else if (len < max - 1) {
            kputchar(c);
            buf[len++] = c;
        }
    }
}

/* ── 명령어 실행 ─────────────────────────────────────────────────────────── */

static void exec(char *line) {
    char *argv[ARGS_MAX];
    int   argc = parse(line, argv);
    if (argc == 0) return;

    for (int i = 0; i < NUM_CMDS; i++) {
        if (str_eq(argv[0], cmds[i].name)) {
            cmds[i].fn(argc, argv);
            return;
        }
    }
    kprint_color("unknown: ", VGA_LIGHT_RED, VGA_BLACK);
    kprint(argv[0]);
    kprint("  (type 'help')\n");
}

/* ── 셸 메인 루프 ────────────────────────────────────────────────────────── */

void shell_run(void) {
    char line[LINE_MAX];
    kprint_color("MyOS Shell - ", VGA_YELLOW, VGA_BLACK);
    kprint("type 'help' for commands\n\n");

    for (;;) {
        kprint_color(cwd_path, VGA_LIGHT_GREEN, VGA_BLACK);
        kprint_color("> ", VGA_LIGHT_GREEN, VGA_BLACK);
        readline(line, LINE_MAX);
        exec(line);
    }
}
