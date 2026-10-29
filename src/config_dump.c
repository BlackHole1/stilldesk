// 诊断用：把传给 CSettingLogic::DealPresetDialogConfig(std::string const&)
// 的服务端「预置弹窗」配置 JSON 打出来，方便确认服务端在推哪些
// (type, errorcode) 组合。
//
// 创建 ~/.config/stilldesk/dump-preset-config（内容随意）再重启 ToDesk 即启用，
// JSON 会追加写入 ~/Library/Logs/StillDesk.config.log。
//
// 读取目标进程的内存全部走 vm_read_overwrite，所以即使对 libc++ std::string
// 的布局猜错了，也不会让宿主进程崩溃。

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/vm_map.h>

static int safe_read(uintptr_t addr, void *dst, size_t len) {
    vm_size_t got = 0;
    if (!addr) return 0;
    kern_return_t kr = vm_read_overwrite(mach_task_self(), (vm_address_t)addr,
                                         (vm_size_t)len, (vm_address_t)dst, &got);
    return kr == KERN_SUCCESS && got == len;
}

void cfg_dump(const void *obj) {
    unsigned char raw[32];
    if (!safe_read((uintptr_t)obj, raw, sizeof raw)) return;

    unsigned long len = 0;
    const char *data = NULL;
    if (raw[23] & 0x80) {                     /* libc++ 短字符串（SSO） */
        len = raw[23] & 0x7f;
        data = (const char *)raw;
    } else {                                  /* 长字符串 */
        uintptr_t ptr = 0;
        memcpy(&ptr, raw, sizeof ptr);
        memcpy(&len, raw + 16, sizeof len);
        data = (const char *)ptr;
    }
    if (len > (1u << 20)) len = 1u << 20;

    static char buf[1 << 20];
    size_t nread = 0;
    if (data && len && safe_read((uintptr_t)data, buf, len)) nread = len;

    char path[1024];
    const char *home = getenv("HOME");
    snprintf(path, sizeof path, "%s/Library/Logs/StillDesk.config.log", home ? home : "/tmp");
    FILE *f = fopen(path, "a");
    if (!f) return;
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    fprintf(f, "\n=== %04d-%02d-%02d %02d:%02d:%02d DealPresetDialogConfig len=%lu read=%zu ===\n",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec, len, nread);
    if (nread) fwrite(buf, 1, nread, f);
    else fprintf(f, "(empty or unreadable)\n");
    fputc('\n', f);
    fclose(f);
}
