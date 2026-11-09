// stilldesk-launch：带上 libStillDesk.dylib 启动真正的 ToDesk 二进制，
// 不改动二进制本身，也不动它的签名。
//
// install.sh 创建独立的 StillDesk.app，保留 ToDesk 原来的启动配置。
// 启动器从同目录的 target-path 读取目标路径，设置 DYLD_INSERT_LIBRARIES，
// 把 stdout/stderr 重定向到日志，然后 execv() 原来的已签名可执行文件。

#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>

#define DYLIB_NAME "libStillDesk.dylib"
#define STDOUT_LOG "Library/Logs/StillDesk.stdout.log"
#define STDOUT_LOG_MAX ((off_t)(8 * 1024 * 1024))

int main(int argc, char **argv) {
    char self[PATH_MAX];
    uint32_t size = sizeof self;
    if (_NSGetExecutablePath(self, &size) != 0) return 120;

    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", self);
    char *slash = strrchr(dir, '/');
    if (!slash) return 121;
    *slash = 0;                                  /* .../Contents/libexec */

    char dylib[PATH_MAX], dylib_real[PATH_MAX];
    char target[PATH_MAX], resolved[PATH_MAX], logpath[PATH_MAX];

    snprintf(dylib, sizeof dylib, "%s/%s", dir, DYLIB_NAME);
    /* 注意：realpath() 原地调用会把结果写坏，必须用第二个缓冲区。 */
    if (realpath(dylib, dylib_real) != NULL) snprintf(dylib, sizeof dylib, "%s", dylib_real);

    char config[PATH_MAX];
    snprintf(config, sizeof config, "%s/target-path", dir);
    FILE *target_file = fopen(config, "r");
    if (!target_file) {
        fprintf(stderr, "StillDesk: missing target-path configuration\n");
        return 122;
    }
    int have_target = fgets(target, sizeof target, target_file) != NULL;
    fclose(target_file);
    if (!have_target) return 122;
    target[strcspn(target, "\r\n")] = 0;
    if (target[0] != '/' || realpath(target, resolved) == NULL || access(resolved, X_OK) != 0) {
        fprintf(stderr, "StillDesk: target executable not found\n");
        return 123;
    }

    /* fail open：dylib 丢了 App 也能启动，只是不过滤 */
    if (access(dylib, R_OK) == 0) setenv("DYLD_INSERT_LIBRARIES", dylib, 1);

    const char *home = getenv("HOME");
    snprintf(logpath, sizeof logpath, "%s/%s", home ? home : "/tmp", STDOUT_LOG);
    struct stat st;
    int fd;
    if (stat(logpath, &st) == 0 && st.st_size > STDOUT_LOG_MAX)
        fd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    else
        fd = open(logpath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }

    argv[0] = resolved;
    execv(resolved, argv);
    return 127;
}
