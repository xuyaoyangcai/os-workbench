#define _GNU_SOURCE

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
#include <errno.h>

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
        if (len >= (int)sizeof(log.name)) len = sizeof(log.name) - 1;
        strncpy(log.name, line + matches[1].rm_so, len);
        log.name[len] = '\0';

        len = matches[2].rm_eo - matches[2].rm_so;
        if (len >= 16) len = 15;
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
        if (!stats->logs) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
    }

    memcpy(&stats->logs[stats->count], log, sizeof(SyscallLog));
    stats->count++;

    return 0;
}

int compare(const void* a, const void* b)
{
    double diff = ((const SyscallLog*)b)->time - ((const SyscallLog*)a)->time;
    return (diff > 0) ? 1 : ((diff < 0) ? -1 : 0);
}

int output(SyscallStats* stats, bool is_end)
{
    if (stats->count == 0) return 0;

    qsort(stats->logs, stats->count, sizeof(SyscallLog), compare);

    double total = 0;
    for (int i = 0; i < stats->count; i++) {
        total += stats->logs[i].time;
    }

    if (total > 0) {
        printf("Total: %.6fs\n", total);
        for (int i = 0; i < stats->count && i < 5; i++) {
            int ratio = (int)(stats->logs[i].time / total * 100);
            printf("%s (%d%%)\n", stats->logs[i].name, ratio);
        }
        if (stats->count > 5) {
            printf("...\n");
        }
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
    if (dup2(pfd[1], STDERR_FILENO) == -1) {
        perror("dup2");
        exit(EXIT_FAILURE);
    }
    close(pfd[1]);

    // 构造 execvp 参数
    char** exec_argv = malloc((argc + 3) * sizeof(char*));
    if (!exec_argv) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    exec_argv[0] = "strace";
    exec_argv[1] = "-T";

    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            exec_argv[i + 1] = argv[i];
        }
        exec_argv[argc + 1] = NULL;
    } else {
        exec_argv[2] = "sleep";
        exec_argv[3] = "1000000000";
        exec_argv[4] = NULL;
    }

    execvp("strace", exec_argv);

    perror("execvp");
    free(exec_argv);
    exit(EXIT_FAILURE);
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
    // 匹配形式: syscall(...) = ... <time>
    const char* pattern = "([a-z0-9_]+)\\(.*\\)\\s*=.*<([0-9.]+)>";
    int rc = regcomp(&reg, pattern, REG_EXTENDED | REG_ICASE);
    if (rc != 0) {
        fprintf(stderr, "Failed to compile regex\n");
        exit(EXIT_FAILURE);
    }

    while (getline(&line, &len, pipe_fp) != -1) {
        SyscallLog* log = extract(line, &reg);
        if (log != NULL) {
            update_stats(log, &stats);
        }

        clock_t now = clock();
        if ((now - prev) * 1000.0 / CLOCKS_PER_SEC > 1000) { // 1秒刷新
            output(&stats, false);
            prev = now;
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
    int pfd[2];
    if (pipe(pfd) != 0) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        child_process(pfd, argc, argv);
    } else {
        parent_process(pfd);
        waitpid(pid, NULL, 0);
    }

    return 0;
}
