#define _GNU_SOURCE

#include <assert.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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
        strncpy(log.name, line + matches[1].rm_so, len);
        log.name[len] = '\0';

        len = matches[2].rm_eo - matches[2].rm_so;
        char time_str[16];
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

    if (total == 0) return 0;

    printf("Total: %fs\n", total);
    for (int i = 0; i < stats->count; i++) {
        int ratio = stats->logs[i].time / total * 100;
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
    close(pfd[0]);
    dup2(pfd[1], STDOUT_FILENO);
    dup2(pfd[1], STDERR_FILENO);
    close(pfd[1]);

    char* exec_argv[argc + 3];
    exec_argv[0] = "strace";
    exec_argv[1] = "-T";
    exec_argv[2] = "-f";
    for (int i = 1; i < argc; i++) {
        exec_argv[i + 2] = argv[i];
    }
    exec_argv[argc + 2] = NULL;

    char path_str[1024] = "PATH=";
    strncat(path_str, getenv("PATH"), sizeof(path_str) - 5);
    char* exec_envp[] = { path_str, NULL };

    execve("/usr/bin/strace", exec_argv, exec_envp);
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
    const char* pattern = "^([a-zA-Z0-9_]+)\\(.*\\) *= .*<([0-9.]+)>";
    assert(regcomp(&reg, pattern, REG_EXTENDED) == 0);

    while (getline(&line, &len, pipe_fp) != -1) {
        SyscallLog* log = extract(line, &reg);
        if (!log) continue;
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
    if (argc < 2) {
        fprintf(stderr, "Usage: %s command [args...]\n", argv[0]);
        return 1;
    }

    int pfd[2];
    assert(pipe(pfd) == 0);

    pid_t pid = fork();
    assert(pid != -1);

    if (pid == 0) {
        child_process(pfd, argc, argv);
    } else {
        parent_process(pfd);
        waitpid(pid, NULL, 0);
    }

    return 0;
}
