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
        if (len >= (int)sizeof(log.name)) len = (int)sizeof(log.name) - 1;
        strncpy(log.name, line + matches[1].rm_so, len);
        log.name[len] = '\0';

        len = matches[2].rm_eo - matches[2].rm_so;
        char time_str[32];
        if (len >= (int)sizeof(time_str)) len = (int)sizeof(time_str) - 1;
        strncpy(time_str, line + matches[2].rm_so, len);
        time_str[len] = '\0';
        log.time = atof(time_str);

        return &log;
    }
    return NULL;
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
        stats->capacity = stats->capacity == 0 ? 16 : stats->capacity * 2;
        stats->logs = realloc(stats->logs, stats->capacity * sizeof(SyscallLog));
        assert(stats->logs != NULL);
    }

    stats->logs[stats->count++] = *log;
    return 0;
}

int compare(const void* a, const void* b)
{
    double diff = ((SyscallLog*)b)->time - ((SyscallLog*)a)->time;
    if (diff > 0) return 1;
    if (diff < 0) return -1;
    return 0;
}

int output(SyscallStats* stats, bool is_end)
{
    if (stats->count == 0) return 0;

    qsort(stats->logs, stats->count, sizeof(SyscallLog), compare);

    double total = 0;
    for (int i = 0; i < stats->count; i++) {
        total += stats->logs[i].time;
    }
    if (total < 1e-9) total = 1; // 防止除0

    printf("Total: %fs\n", total);
    int max_show = stats->count < 5 ? stats->count : 5;
    for (int i = 0; i < max_show; i++) {
        int ratio = (int)(stats->logs[i].time / total * 100);
        printf("%s (%d%%)\n", stats->logs[i].name, ratio);
    }
    if (stats->count > 5) {
        printf("...\n");
    }

    for (int i = 0; i < 80; i++) putchar('\0');
    if (!is_end) printf("====================\n");
    fflush(stdout);

    return 0;
}

void child_process(int pfd[], int argc, char* argv[])
{
    // 保存原来的stderr
    int saved_stderr = dup(STDERR_FILENO);
    if (saved_stderr < 0) {
        perror("dup");
        _exit(1);
    }

    // 关闭管道读端
    close(pfd[0]);
    // 重定向 stderr 到管道写端
    if (dup2(pfd[1], STDERR_FILENO) < 0) {
        perror("dup2");
        _exit(1);
    }
    close(pfd[1]);

    char* exec_argv[argc + 2];
    exec_argv[0] = "strace";
    exec_argv[1] = "-T";
    for (int i = 1; i < argc; i++) {
        exec_argv[i + 1] = argv[i];
    }
    exec_argv[argc + 1] = NULL;

    execve("/usr/bin/strace", exec_argv, environ);

    // execve失败：恢复原来的stderr并输出错误
    if (dup2(saved_stderr, STDERR_FILENO) < 0) {
        _exit(1);
    }
    close(saved_stderr);
    perror("execve");
    _exit(1);
}

void parent_process(int pfd[])
{
    close(pfd[1]);
    FILE* pipe_fp = fdopen(pfd[0], "r");
    assert(pipe_fp != NULL);

    SyscallStats stats = {NULL, 0, 0};
    char* line = NULL;
    size_t len = 0;
    clock_t prev = clock();

    // 匹配形如：read(3, "a", 1) = 1 <0.000020>
    const char* pattern = "^([a-z0-9_]+)\\(.*\\) = .* <([0-9.]+)>\\s*$";
    regex_t reg;
    int rc = regcomp(&reg, pattern, REG_EXTENDED | REG_NEWLINE);
    assert(rc == 0);

    while (getline(&line, &len, pipe_fp) != -1) {
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
    fclose(pipe_fp);
}

int main(int argc, char* argv[])
{
    if (argc == 1) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        exit(1); // 修复：正确退出而非无限循环
    }

    int pfd[2];
    assert(pipe(pfd) == 0);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        child_process(pfd, argc, argv);
    } else {
        parent_process(pfd);
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            if (exit_code != 0) {
                fprintf(stderr, "Command exited with status %d\n", exit_code);
                return exit_code;
            }
        } else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            fprintf(stderr, "Command terminated by signal %d\n", sig);
            return 128 + sig;
        }
    }
    return 0;
}