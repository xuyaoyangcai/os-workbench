#define _GNU_SOURCE

#include <assert.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    stats->logs[stats->count++] = *log;
    return 0;
}

int compare(const void* a, const void* b)
{
    double diff = ((SyscallLog*)b)->time - ((SyscallLog*)a)->time;
    if (diff > 0) return 1;
    else if (diff < 0) return -1;
    else return 0;
}

int output(SyscallStats* stats, int top)
{
    if (stats->count == 0) return -1;

    double total = 0.0;
    for (int i = 0; i < stats->count; i++) {
        total += stats->logs[i].time;
    }

    qsort(stats->logs, stats->count, sizeof(SyscallLog), compare);

    if (top > stats->count) top = stats->count;

    for (int i = 0; i < top; i++) {
        int ratio = (int)((stats->logs[i].time / total) * 100 + 0.5);
        printf("%s (%d%%)\n", stats->logs[i].name, ratio);
    }

    return 0;
}

int child_process(int pfd[], int argc, char* argv[])
{
    close(pfd[0]);  // 关闭读端
    dup2(pfd[1], STDOUT_FILENO);
    dup2(pfd[1], STDERR_FILENO);
    close(pfd[1]);

    int new_argc = argc + 3;
    char** exec_argv = malloc(sizeof(char*) * (new_argc + 1));
    assert(exec_argv != NULL);

    exec_argv[0] = "strace";
    exec_argv[1] = "-T";
    exec_argv[2] = "-o";
    exec_argv[3] = "-";
    for (int i = 1; i < argc; i++) {
        exec_argv[i + 3] = argv[i];
    }
    exec_argv[new_argc] = NULL;

    execve("/usr/bin/strace", exec_argv, environ);
    perror("execve");
    exit(1);
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s command [args...]\n", argv[0]);
        return 1;
    }

    int pfd[2];
    if (pipe(pfd) < 0) {
        perror("pipe");
        exit(1);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }

    if (pid == 0) {
        // 子进程
        child_process(pfd, argc, argv);
    }

    // 父进程
    close(pfd[1]);

    regex_t reg;
    // 匹配形如 syscall_name(...) <time>
    const char* pattern = "^([a-z0-9_]+)\\(.*<([0-9.]+)>";
    if (regcomp(&reg, pattern, REG_EXTENDED | REG_ICASE) != 0) {
        fprintf(stderr, "Failed to compile regex\n");
        exit(1);
    }

    SyscallStats stats = {0};

    char* line = NULL;
    size_t len = 0;
    FILE* fp = fdopen(pfd[0], "r");
    assert(fp != NULL);

    while (getline(&line, &len, fp) != -1) {
        SyscallLog* log = extract(line, &reg);
        if (log) {
            update_stats(log, &stats);
        }
    }
    free(line);
    fclose(fp);

    int status;
    waitpid(pid, &status, 0);
    regfree(&reg);

    output(&stats, 5);

    free(stats.logs);

    return 0;
}
