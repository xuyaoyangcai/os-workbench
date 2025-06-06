#define _GNU_SOURCE

#include <assert.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

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

// 从一行中提取 syscall 名和时间
SyscallLog* extract(char* line, regex_t* reg)
{
    regmatch_t matches[3];
    static SyscallLog log;

    if (regexec(reg, line, 3, matches, 0) == 0) {
        int len = matches[1].rm_eo - matches[1].rm_so;
        if (len >= (int)sizeof(log.name)) len = sizeof(log.name) - 1;
        strncpy(log.name, line + matches[1].rm_so, len);
        log.name[len] = '\0';

        len = matches[2].rm_eo - matches[2].rm_so;
        char time_str[32];
        if (len >= (int)sizeof(time_str)) len = sizeof(time_str) - 1;
        strncpy(time_str, line + matches[2].rm_so, len);
        time_str[len] = '\0';

        log.time = atof(time_str);

        return &log;
    }
    return NULL;
}

// 更新统计数组
int update_stats(SyscallLog* log, SyscallStats* stats)
{
    for (int i = 0; i < stats->count; i++) {
        if (strcmp(stats->logs[i].name, log->name) == 0) {
            stats->logs[i].time += log->time;
            return 0;
        }
    }

    if (stats->count == stats->capacity) {
        stats->capacity = stats->capacity == 0 ? 16 : stats->capacity * 2;
        stats->logs = realloc(stats->logs, stats->capacity * sizeof(SyscallLog));
        assert(stats->logs != NULL);
    }

    memcpy(&stats->logs[stats->count], log, sizeof(SyscallLog));
    stats->count++;
    return 0;
}

// 按耗时降序排序
int compare(const void* a, const void* b)
{
    double diff = ((SyscallLog*)b)->time - ((SyscallLog*)a)->time;
    if (diff > 0) return 1;
    if (diff < 0) return -1;
    return 0;
}

// 输出当前统计信息
int output(SyscallStats* stats, bool is_end)
{
    if (stats->count == 0) return 0;

    qsort(stats->logs, stats->count, sizeof(SyscallLog), compare);

    double total = 0;
    for (int i = 0; i < stats->count; i++) {
        total += stats->logs[i].time;
    }

    printf("Total: %.6fs\n", total);

    for (int i = 0; i < stats->count && i < 5; i++) {
        int ratio = (int)((stats->logs[i].time / total) * 100 + 0.5);
        printf("%s (%d%%)\n", stats->logs[i].name, ratio);
    }
    if (stats->count > 5) {
        printf("...\n");
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

// 子进程：启动 strace 并把 stderr 重定向到管道写端
void child_process(int pfd[], int argc, char* argv[])
{
    close(pfd[0]);
    int ret = dup2(pfd[1], STDERR_FILENO);
    assert(ret != -1);
    close(pfd[1]);

    // 构造 exec 参数，strace -T <command> ...
    char** exec_argv = malloc(sizeof(char*) * (argc + 2));
    assert(exec_argv != NULL);
    exec_argv[0] = "strace";
    exec_argv[1] = "-T";
    for (int i = 1; i < argc; i++) {
        exec_argv[i + 1] = argv[i];
    }
    exec_argv[argc + 1] = NULL;

    execvp("strace", exec_argv);

    perror("execvp strace failed");
    _exit(127);
}

// 父进程：从管道读 strace 输出，解析并统计
void parent_process(int pfd[])
{
    close(pfd[1]);
    FILE* fp = fdopen(pfd[0], "r");
    assert(fp != NULL);

    SyscallStats stats = { NULL, 0, 0 };
    char* line = NULL;
    size_t len = 0;

    // 匹配示例行： openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3 <0.000050>
    // 正则： syscall名 + <时间>
    const char* pattern = "^([a-z0-9_]+)\\([^)]*\\) = .* <([0-9.]+)>";
    regex_t reg;
    int rc = regcomp(&reg, pattern, REG_EXTENDED);
    assert(rc == 0);

    clock_t prev = clock();

    while (getline(&line, &len, fp) != -1) {
        SyscallLog* log = extract(line, &reg);
        if (log != NULL) {
            update_stats(log, &stats);
        }

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
    fclose(fp);
}

// 无参数时阻塞等待不退出
void wait_forever()
{
    sigset_t mask;
    sigemptyset(&mask);
    while (1) {
        sigsuspend(&mask);
    }
}

int main(int argc, char* argv[])
{
    if (argc == 1) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        wait_forever();
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
