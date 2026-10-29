// StillDesk：关掉 ToDesk（macOS）里的推销弹窗。
//
// stilldesk-launch（见 launcher.c）通过 DYLD_INSERT_LIBRARIES 把 StillDesk
// 注入到真正的 ToDesk 进程里。磁盘上除了 ToDesk 自己的 config.ini 之外什么都不改，
// 二进制的代码签名也从不触碰，所以 App 的身份（TCC 授权、钥匙串条目、
// 特权辅助进程）全部保持原样。
//
// 一共处理两套互不相干的弹窗：
//
//  1) 预置弹窗：由服务端和本地配置驱动的弹窗，唯一决策点是
//     MainDlgPresetDialogLogic::HandlePresetDialogCandidate()。
//     镜像偏移 0x44cb34 处的四条 mov 被换成一个 16 字节分支岛，
//     跳到 trampoline.s 里的 gate_trampoline，由它去问规则引擎：
//     要么重放这四条指令、从 0x44cb44 继续执行原函数体，
//     要么执行函数自己的尾声（0x10044cf50）后返回。
//
//  2) 到期提醒：CMainDlg::processOverdueNotify()、
//     CPlaywndUI::processOverdueNotify() 和 CMainDlg::processExpiringNotify()
//     的首条指令都换成 ret 来禁用，同时把 config.ini 里的 overduetipdate
//     推到很远的将来，让 JavaScript 侧的判断也一起失效。
//
// 每个补丁点在写入前都做逐字节校验。ToDesk 版本对不上时，
// StillDesk 只打印 ABORT，App 保持原样。

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <libkern/OSCacheControl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* 补丁点（ToDesk 4.10.1.0 build 2591，arm64，偏移以 Mach-O 镜像基址为准；*/
/* App 升级后重新运行 tools/find-offsets.sh）                          */
/* ------------------------------------------------------------------ */

/* 预置弹窗的决策点 */
#define GATE_VMOFF        0x44cb34ULL
#define GATE_CONTINUE     0x44cb44ULL
#define INSN_MOV_X20_X3   0xAA0303F4u
#define INSN_MOV_X21_X2   0xAA0203F5u
#define INSN_MOV_X24_X1   0xAA0103F8u
#define INSN_MOV_X22_X0   0xAA0003F6u

/* CSettingLogic::DealPresetDialogConfig(std::string const&) 的诊断钩子 */
#define CFG_VMOFF         0xC90B6CULL
#define CFG_CONTINUE      0xC90B8CULL

/* 到期提醒：各函数的首条指令 */
#define OVERDUE_MAIN_VMOFF    0x7EEFCULL
#define OVERDUE_MAIN_INSN     0xD10603FFu
#define OVERDUE_VIEWER_VMOFF  0x27E518ULL
#define OVERDUE_VIEWER_INSN   0xD10643FFu
#define EXPIRING_MAIN_VMOFF   0x7F5DCULL
#define EXPIRING_MAIN_INSN    0xA9BA6FFCu

#define INSN_RET 0xD65F03C0u

/* 真正的 App 二进制，即进程内 dyld 看到的那个 */
#define TARGET_IMAGE_SUFFIX "Contents/MacOS/ToDesk"
#define TARGET_APP_DEFAULT  "/Applications/ToDesk.app"

extern void gate_trampoline(void);
uint64_t gate_continue_addr = 0;

extern void cfg_trampoline(void);
uint64_t cfg_continue_addr = 0;

/* ------------------------------------------------------------------ */
/* 路径                                                                */
/* ------------------------------------------------------------------ */

static const char *home_dir(void) {
    const char *h = getenv("HOME");
    return h ? h : "/tmp";
}

static void log_path(char *out, size_t n) {
    snprintf(out, n, "%s/Library/Logs/StillDesk.log", home_dir());
}

static void legacy_path(char *out, size_t n, const char *base) {
    snprintf(out, n, "%s/%s", home_dir(), base);
}

static void config_path(char *out, size_t n, const char *name) {
    snprintf(out, n, "%s/.config/stilldesk/%s", home_dir(), name);
}

/* 打开 <配置目录>/<name>，找不到时回退到 StillDesk 之前的旧路径，
   这样早期版本写下的规则仍然有效。 */
static FILE *open_rules(const char *name, const char *legacy_base, char *used, size_t used_n) {
    char modern[1024];
    config_path(modern, sizeof modern, name);
    FILE *f = fopen(modern, "r");
    if (f) { snprintf(used, used_n, "%s", modern); return f; }
    if (legacy_base && *legacy_base) {
        char old[1024];
        legacy_path(old, sizeof old, legacy_base);
        f = fopen(old, "r");
        if (f) { snprintf(used, used_n, "%s", old); return f; }
    }
    snprintf(used, used_n, "%s", modern);
    return NULL;
}

static void log_line(const char *fmt, ...) {
    char path[1024];
    log_path(path, sizeof path);
    FILE *f = fopen(path, "a");
    if (!f) return;
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);
    fprintf(f, "[%s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* 预置弹窗规则                                                        */
/*                                                                     */
/*   all                拦截全部预置弹窗                               */
/*   allow-all          全部放行（仍然记日志）                          */
/*   type:N             拦截类型 N 的弹窗                              */
/*   pair:T/E           只拦截（type=T, errorcode=E）                  */
/*   allow:type:N       例外，优先于任何拦截规则                       */
/*   allow:pair:T/E     例外，优先于任何拦截规则                       */
/*                                                                     */
/* 完全没有规则文件表示拦截全部。                                      */
/* ------------------------------------------------------------------ */

#define MAX_RULES 64

static int      g_block_all = 1;
static int      g_type_rules[MAX_RULES];
static unsigned g_pair_rules[MAX_RULES];       /* (type << 16) | errorcode */
static int      g_n_type = 0, g_n_pair = 0;
static int      g_allow_type[MAX_RULES];
static unsigned g_allow_pair[MAX_RULES];
static int      g_n_allow_type = 0, g_n_allow_pair = 0;
static long     g_rules_stamp = -1;

static void maybe_reload_rules(void);

static void load_preset_rules(void) {
    char used[1024];
    g_n_type = g_n_pair = g_n_allow_type = g_n_allow_pair = 0;
    FILE *f = open_rules("preset-blocklist", ".todesk-preset-blocklist", used, sizeof used);
    if (!f) {
        g_block_all = 1;
        log_line("preset rules: none at %s -> block all", used);
        return;
    }
    g_block_all = 0;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == 0) continue;
        if (!strncmp(p, "allow-all", 9)) { g_block_all = 0; continue; }
        if (p[0] == 'a' && p[1] == 'l' && p[2] == 'l' &&
            (p[3] == '\n' || p[3] == '\r' || p[3] == 0 || p[3] == ' ' || p[3] == '\t')) {
            g_block_all = 1;
            continue;
        }
        if (!strncmp(p, "allow:type:", 11)) {
            if (g_n_allow_type < MAX_RULES) g_allow_type[g_n_allow_type++] = atoi(p + 11);
            continue;
        }
        if (!strncmp(p, "allow:pair:", 11)) {
            int t = 0, e = 0;
            if (sscanf(p + 11, "%d/%d", &t, &e) == 2 && g_n_allow_pair < MAX_RULES)
                g_allow_pair[g_n_allow_pair++] = ((unsigned)t << 16) | (unsigned)(e & 0xFFFF);
            continue;
        }
        if (!strncmp(p, "type:", 5)) {
            if (g_n_type < MAX_RULES) g_type_rules[g_n_type++] = atoi(p + 5);
            continue;
        }
        if (!strncmp(p, "pair:", 5)) {
            int t = 0, e = 0;
            if (sscanf(p + 5, "%d/%d", &t, &e) == 2 && g_n_pair < MAX_RULES)
                g_pair_rules[g_n_pair++] = ((unsigned)t << 16) | (unsigned)(e & 0xFFFF);
            continue;
        }
    }
    fclose(f);
    log_line("preset rules: %s all=%d types=%d pairs=%d allow_types=%d allow_pairs=%d",
             used, g_block_all, g_n_type, g_n_pair, g_n_allow_type, g_n_allow_pair);
}

/* 规则文件的 mtime 一变就重读：调规则不用重启。 */
static void maybe_reload_rules(void) {
    char used[1024];
    struct stat st;
    long stamp;
    FILE *probe = open_rules("preset-blocklist", ".todesk-preset-blocklist", used, sizeof used);
    if (probe) fclose(probe);      /* 这里只关心解析出来的路径 */
    if (stat(used, &st) == 0)
        stamp = (long)st.st_mtimespec.tv_sec * 1000000000L + (long)st.st_mtimespec.tv_nsec;
    else
        stamp = -1;
    if (stamp == g_rules_stamp) return;
    g_rules_stamp = stamp;
    load_preset_rules();
}

int gate_should_block(void *self, void *owner, int type, int errorcode) {
    (void)self; (void)owner;
    maybe_reload_rules();
    unsigned key = ((unsigned)type << 16) | (unsigned)(errorcode & 0xFFFF);
    int blocked = g_block_all ? 1 : 0;
    for (int i = 0; i < g_n_type && !blocked; i++)
        if (g_type_rules[i] == type) blocked = 1;
    for (int i = 0; i < g_n_pair && !blocked; i++)
        if (g_pair_rules[i] == key) blocked = 1;
    if (blocked) {
        int allowed = 0;
        for (int i = 0; i < g_n_allow_type && !allowed; i++)
            if (g_allow_type[i] == type) allowed = 1;
        for (int i = 0; i < g_n_allow_pair && !allowed; i++)
            if (g_allow_pair[i] == key) allowed = 1;
        if (allowed) blocked = 0;
    }
    log_line("preset gate: type=%d errorcode=%d -> %s", type, errorcode, blocked ? "BLOCK" : "show");
    return blocked;
}

/* ------------------------------------------------------------------ */
/* 到期提醒规则：overdue / expiring，默认两类都隐藏                    */
/* ------------------------------------------------------------------ */

static int g_hide_overdue = 1;
static int g_hide_expiring = 1;

static void load_notify_rules(void) {
    char used[1024];
    FILE *f = open_rules("hide-notify", ".todesk-hide-notify", used, sizeof used);
    if (!f) {
        log_line("notify rules: none at %s -> hide overdue+expiring", used);
        return;
    }
    g_hide_overdue = 0;
    g_hide_expiring = 0;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, "overdue"))  g_hide_overdue = 1;
        if (strstr(line, "expiring")) g_hide_expiring = 1;
    }
    fclose(f);
    log_line("notify rules: %s overdue=%d expiring=%d", used, g_hide_overdue, g_hide_expiring);
}

/* ------------------------------------------------------------------ */
/* 打补丁用的辅助函数                                                  */
/* ------------------------------------------------------------------ */

static int make_writable(void *addr, size_t len) {
    vm_address_t page = (vm_address_t)addr & ~(vm_page_size - 1ULL);
    vm_address_t end  = ((vm_address_t)addr + len + vm_page_size - 1) & ~(vm_page_size - 1ULL);
    return vm_protect(mach_task_self(), page, (vm_size_t)(end - page), FALSE,
                      VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) == KERN_SUCCESS;
}

static void make_exec(void *addr, size_t len) {
    vm_address_t page = (vm_address_t)addr & ~(vm_page_size - 1ULL);
    vm_address_t end  = ((vm_address_t)addr + len + vm_page_size - 1) & ~(vm_page_size - 1ULL);
    vm_protect(mach_task_self(), page, (vm_size_t)(end - page), FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    sys_icache_invalidate(addr, len);
}

/* 用 "ldr x9, #8 ; br x9 ; .quad <tramp>" 覆盖 n_expect 条指令，
   并记下原函数体该从哪儿继续执行。 */
static int install_island(uintptr_t base, uint64_t vmoff, const uint32_t *expect, int n_expect,
                          void *tramp, uint64_t continue_vmoff, uint64_t *continue_out,
                          const char *what) {
    uint32_t *site = (uint32_t *)(base + vmoff);
    uint32_t got[8];
    memcpy(got, site, sizeof(uint32_t) * (size_t)n_expect);
    if (got[0] == 0x58000049u && got[1] == 0xD61F0120u) {
        log_line("%s: already patched", what);
        *continue_out = (uint64_t)(base + continue_vmoff);
        return 1;
    }
    for (int i = 0; i < n_expect; i++) {
        if (got[i] != expect[i]) {
            log_line("%s: ABORT unexpected code at %d: %08x (want %08x)", what, i, got[i], expect[i]);
            return 0;
        }
    }
    size_t hole = (size_t)n_expect * 4;
    if (!make_writable(site, hole)) { log_line("%s: ABORT vm_protect(write)", what); return 0; }
    uint32_t *buf = calloc(1, hole);
    if (!buf) { log_line("%s: ABORT out of memory", what); return 0; }
    uint64_t t = (uint64_t)(uintptr_t)tramp;
    buf[0] = 0x58000049u;
    buf[1] = 0xD61F0120u;
    memcpy((char *)buf + 8, &t, 8);
    for (size_t i = 16; i < hole; i += 4) *(uint32_t *)((char *)buf + i) = 0xD503201Fu;
    memcpy(site, buf, hole);
    free(buf);
    make_exec(site, hole);
    *continue_out = (uint64_t)(base + continue_vmoff);
    log_line("%s: island installed (%zu bytes) trampoline=%p continue=%p",
             what, hole, tramp, (void *)*continue_out);
    return 1;
}

/* 把函数首条指令换成 ret。 */
static void install_ret_hook(uintptr_t base, uint64_t vmoff, uint32_t expect, const char *name) {
    uint32_t *site = (uint32_t *)(base + vmoff);
    uint32_t got = *site;
    if (got == INSN_RET) { log_line("hook %s: already ret", name); return; }
    if (got != expect) {
        log_line("hook %s: ABORT unexpected %08x (want %08x)", name, got, expect);
        return;
    }
    if (!make_writable(site, 4)) { log_line("hook %s: ABORT vm_protect", name); return; }
    *site = INSN_RET;
    make_exec(site, 4);
    log_line("hook %s: patched -> ret (was %08x)", name, got);
}

/* ------------------------------------------------------------------ */
/* config.ini 里的 overduetipdate（"下次可以再烦你"的日期）改成 2099    */
/* ------------------------------------------------------------------ */

static void ensure_overdue_tip_date(const char *main_image_path) {
    char cfg[2048];
    const char *slash = strrchr(main_image_path, '/');   /* .../Contents/MacOS/ToDesk */
    if (!slash) return;
    size_t keep = (size_t)(slash - main_image_path);
    if (keep >= sizeof cfg - 32) return;
    memcpy(cfg, main_image_path, keep);
    cfg[keep] = 0;
    char *macos = strrchr(cfg, '/');
    if (!macos) return;
    *macos = 0;                                          /* .../Contents */
    strncat(cfg, "/config.ini", sizeof cfg - strlen(cfg) - 1);

    FILE *f = fopen(cfg, "r");
    if (!f) { log_line("overdue date: cannot open %s", cfg); return; }
    static char buf[1 << 20];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    if (memmem(buf, n, "20991231", 8)) { log_line("overdue date: already far future"); return; }

    char out[1 << 20];
    size_t o = 0, i = 0;
    int changed = 0;
    while (i < n) {
        char *line_end = memchr(buf + i, '\n', n - i);
        size_t len = line_end ? (size_t)(line_end - (buf + i)) + 1 : n - i;
        const char *line = buf + i;
        if (len > 14 && !strncmp(line, "overduetipdate", 14)) {
            const char *eq = memchr(line, '=', len);
            if (eq) {
                size_t j = (size_t)(eq - line) + 1;
                while (j < len && (line[j] == ' ' || line[j] == '\t')) j++;
                size_t valstart = j;
                while (j < len && line[j] != '\n' && line[j] != '\r' && line[j] != ' ') j++;
                size_t valend = j;
                memcpy(out + o, line, valstart); o += valstart;
                memcpy(out + o, "20991231", 8);  o += 8;
                if (valend - valstart > 8) {                 /* 保持列对齐 */
                    memset(out + o, ' ', valend - valstart - 8);
                    o += valend - valstart - 8;
                }
                memcpy(out + o, line + valend, len - valend); o += len - valend;
                changed = 1;
                i += len;
                continue;
            }
        }
        memcpy(out + o, line, len); o += len; i += len;
    }
    if (!changed) { log_line("overdue date: key not found"); return; }
    f = fopen(cfg, "w");
    if (!f) { log_line("overdue date: cannot write %s", cfg); return; }
    fwrite(out, 1, o, f);
    fclose(f);
    log_line("overdue date: set overduetipdate -> 20991231");
}

/* ------------------------------------------------------------------ */
/* 入口                                                               */
/* ------------------------------------------------------------------ */

__attribute__((constructor)) static void stilldesk_init(void) {
    if (getenv("STILLDESK_OFF")) { log_line("STILLDESK_OFF set, doing nothing"); return; }
    unsetenv("DYLD_INSERT_LIBRARIES");       /* 不要泄漏给子进程 */

    g_rules_stamp = 0;                        /* 强制首次加载 */
    maybe_reload_rules();
    load_notify_rules();

    const char *want = TARGET_IMAGE_SUFFIX;
    size_t lw = strlen(want);
    uint32_t n = _dyld_image_count();
    for (uint32_t i = 0; i < n; i++) {
        const char *name = _dyld_get_image_name(i);
        if (!name) continue;
        size_t ln = strlen(name);
        if (ln < lw || strcmp(name + ln - lw, want) != 0) continue;

        const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(i);
        if (!mh || mh->magic != MH_MAGIC_64) { log_line("main image header invalid"); return; }
        if (mh->cputype != CPU_TYPE_ARM64) { log_line("skip: cputype %d (arm64 only)", mh->cputype); return; }
        log_line("target: %s", name);

        uintptr_t base = (uintptr_t)mh;

        /* 1) 预置弹窗 */
        gate_continue_addr = (uint64_t)(base + GATE_CONTINUE);
        static const uint32_t expect_gate[4] = {
            INSN_MOV_X20_X3, INSN_MOV_X21_X2, INSN_MOV_X24_X1, INSN_MOV_X22_X0 };
        install_island(base, GATE_VMOFF, expect_gate, 4,
                       (void *)&gate_trampoline, GATE_CONTINUE, &gate_continue_addr, "preset gate");

        /* 1b) 可选诊断：把服务端下发的预置弹窗配置 JSON 打出来 */
        char marker[1024];
        config_path(marker, sizeof marker, "dump-preset-config");
        if (access(marker, R_OK) != 0) {
            char legacy[1024];
            legacy_path(legacy, sizeof legacy, ".todesk-dump-config");
            if (access(legacy, R_OK) == 0) snprintf(marker, sizeof marker, "%s", legacy);
        }
        if (access(marker, R_OK) == 0) {
            static const uint32_t expect_cfg[8] = {
                0xA9BA6FFCu, 0xA90167FAu, 0xA9025FF8u, 0xA90357F6u,
                0xA9044FF4u, 0xA9057BFDu, 0x910143FDu, 0xD10AC3FFu };
            install_island(base, CFG_VMOFF, expect_cfg, 8,
                           (void *)&cfg_trampoline, CFG_CONTINUE, &cfg_continue_addr,
                           "preset config dump");
        }

        /* 2) 到期提醒 */
        if (g_hide_overdue) {
            ensure_overdue_tip_date(name);
            install_ret_hook(base, OVERDUE_MAIN_VMOFF,   OVERDUE_MAIN_INSN,   "overdue-main");
            install_ret_hook(base, OVERDUE_VIEWER_VMOFF, OVERDUE_VIEWER_INSN, "overdue-viewer");
        }
        if (g_hide_expiring) {
            install_ret_hook(base, EXPIRING_MAIN_VMOFF, EXPIRING_MAIN_INSN, "expiring-main");
        }
        log_line("ready");
        return;
    }
    log_line("skip: ToDesk main image not found (helper process?)");
}
