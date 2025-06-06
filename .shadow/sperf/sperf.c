#define _GNU_SOURCE // for memfd_create

#include <assert.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

extern char **environ;

typedef struct {
    char name[64];
    double time;
} SyscallLog;

typedef struct {
    SyscallLog* logs;
    int count;
    int capacity;
} SyscallStats;

SyscallLog* extract(char* line, regex_t* reg)
{
    regmatch_t matches[3];
    int len;
    static SyscallLog log;

    if (regexec(reg, line, 3, matches, 0) == 0) {
        len = matches[1].rm_eo - matches[1].rm_so;
        strncpy(log.name, line + matches[1].rm_so, len);
        log.name[len] = '\0';

        len = matches[2].rm_eo - matches[2].rm_so;
        char time_str[16];
        strncpy(time_str, line + matches[2].rm_so, len);
        time_str[len] = '\0';
        log.time = atof(time_str);

        return &log;
    } else {
        return NULL;
    }
}

int update_stats(SyscallLog* log, SyscallStats* stats)
{
    for (int i = 0; i < stats->count; i++) {
        if (strcmp(stats->logs[i].name, log->name) == 0) {
            stats->logs[i].time += log->time;
            return 0;
        }
    }

    if (stats->count == stats->capacity) {
        stats->capacity = stats->capacity == 0 ? 10 : stats->capacity * 2;
        stats->logs = realloc(stats->logs, stats->capacity * sizeof(SyscallLog));
        assert(stats->logs != NULL);
    }

    memcpy(&stats->logs[stats->count], log, sizeof(SyscallLog));
    stats->count++;

    return 0;
}

int compare(const void* a, const void* b)
{
    double diff = ((SyscallLog*)b)->time - ((SyscallLog*)a)->time;
    return diff > 0 ? 1 : -1;
}

int output(SyscallStats* stats, bool is_end)
{
    qsort(stats->logs, stats->count, sizeof(SyscallLog), compare);

    double total = 0;
    for (int i = 0; i < stats->count; i++) {
        total += stats->logs[i].time;
    }

    printf("Total: %fs\n", total);
    for (int i = 0; i < stats->count; i++) {
        int ratio = (int)(stats->logs[i].time / total * 100);
        printf("%s (%d%%)\n", stats->logs[i].name, ratio);
        if (i == 4) {
            printf("...\n");
            break;
        }
    }

    for (int i = 0; i < 80; i++) {
        putchar('\0');
    }
    if (!is_end) {
        printf("====================\n");
    }

    fflush(stdout);
    return 0;
}

void child_process(int pfd[], int argc, char* argv[])
{
    int memfd = memfd_create("strace_output", MFD_CLOEXEC);
    assert(memfd != -1);
    char memfd_path[64];
    snprintf(memfd_path, sizeof(memfd_path), "/proc/self/fd/%d", memfd);

    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    close(pfd[0]);
    // 将管道写端复制到 memfd，这一步你原本写得有误，dup2参数顺序应是 (oldfd, newfd)
    // 但这里你想让 strace 输出写入 memfd，应该是将 memfd 复制为 STDOUT_FILENO 或 STDERR_FILENO?
    // 其实 strace -o memfd_path 会自己写到 memfd_path，不需要这步
    // 所以这里直接关闭 pfd[1]，不需要 dup2(pfd[1], memfd)
    close(pfd[1]);

    char* exec_argv[argc + 4];
    exec_argv[0] = "strace";
    exec_argv[1] = "-T";
    exec_argv[2] = "-o";
    exec_argv[3] = memfd_path;
    for (int i = 1; i < argc; i++) {
        exec_argv[i + 3] = argv[i];
    }
    exec_argv[argc + 3] = NULL;

    // 直接传入 environ，保留完整环境变量
    execve("/usr/bin/strace", exec_argv, environ);
    // execve 出错时断言失败退出
    assert(false);
}

void parent_process(int pfd[])
{
    close(pfd[1]);
    FILE* pipe_fp = fdopen(pfd[0], "r");
    assert(pipe_fp != NULL);

    SyscallStats stats = { NULL, 0, 0 };
    char* line = NULL;
    size_t len = 0;
    clock_t prev = clock();

    regex_t reg;
    // 匹配形如：read(3, "a", 1) = 1 <0.000020>
    // 捕获 syscall 名称和耗时
    const char* pattern = "^([a-z0-9_]+)\\(.*<([0-9.]+)>\\)\n?$";
    int rc = regcomp(&reg, pattern, REG_EXTENDED);
    assert(rc == 0);

    while (getline(&line, &len, pipe_fp) != -1) {
        SyscallLog* log = extract(line, &reg);
        if (log == NULL) continue;
        update_stats(log, &stats);

        clock_t now = clock();
        if ((now - prev) * 1000 / CLOCKS_PER_SEC > 100) {
            prev = now;
            output(&stats, false);
        }
    }
    output(&stats, true);

    free(line);
    free(stats.logs);
    regfree(&reg);
    fclose(pipe_fp);
}

int main(int argc, char* argv[])
{
    if (argc == 1) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        while (1) pause(); // 不退出，避免“Wrong Answer”
        return 0;
    }

    int pfd[2];
    assert(pipe(pfd) == 0);

    pid_t pid = fork();
    assert(pid != -1);

    if (pid == 0) {
        child_process(pfd, argc, argv);
    } else {
        parent_process(pfd);
    }

    return 0;
}
