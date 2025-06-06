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
#include <fcntl.h>
#include <sys/wait.h>

extern char **environ;

typedef struct {
    char name[32];
    double total_time;
    double percentage;
} SyscallStat;

#define MAX_SYSCALLS 1024
#define TOP_N 5

SyscallStat stats[MAX_SYSCALLS];
int syscall_count = 0;

// 排序函数：按总耗时降序
int compare_syscall_stats(const void *a, const void *b) {
    const SyscallStat *sa = (const SyscallStat *)a;
    const SyscallStat *sb = (const SyscallStat *)b;
    return (sb->total_time > sa->total_time) - (sb->total_time < sa->total_time);
}

// 查找或新增系统调用记录
int find_or_create_stat(const char *name) {
    for (int i = 0; i < syscall_count; i++) {
        if (strcmp(stats[i].name, name) == 0) {
            return i;
        }
    }

    if (syscall_count < MAX_SYSCALLS) {
        strncpy(stats[syscall_count].name, name, sizeof(stats[syscall_count].name) - 1);
        stats[syscall_count].total_time = 0;
        stats[syscall_count].percentage = 0;
        return syscall_count++;
    }

    return -1;
}

// 解析 strace 行
int parse_strace_line(const char *line, char *name_out, double *time_out) {
    const char *lp = strchr(line, '(');
    const char *lt = strchr(line, '<');
    const char *gt = strchr(line, '>');

    if (!lp || !lt || !gt || lt >= gt || lp <= line) return 0;

    int len = lp - line;
    if (len >= 32) return 0;

    strncpy(name_out, line, len);
    name_out[len] = '\0';

    *time_out = atof(lt + 1);
    return 1;
}

// 打印 top 5 统计
void print_stats() {
    double total = 0.0;
    for (int i = 0; i < syscall_count; i++) {
        total += stats[i].total_time;
    }

    for (int i = 0; i < syscall_count; i++) {
        stats[i].percentage = total > 0 ? (stats[i].total_time * 100.0 / total) : 0.0;
    }

    qsort(stats, syscall_count, sizeof(SyscallStat), compare_syscall_stats);

    int limit = syscall_count < TOP_N ? syscall_count : TOP_N;
    for (int i = 0; i < limit; i++) {
        printf("%s (%d%%)\n", stats[i].name, (int)(stats[i].percentage + 0.5));
    }

    for (int i = 0; i < 80; i++) putchar('\0');
    fflush(stdout);
}

// 子进程：执行 strace -T
void child_process(int pfd[], int argc, char *argv[]) {
    close(pfd[0]);
    dup2(pfd[1], STDERR_FILENO);
    close(pfd[1]);

    char **args = malloc((argc + 3) * sizeof(char *));
    args[0] = "strace";
    args[1] = "-T";
    for (int i = 1; i < argc; i++) {
        args[i + 1] = argv[i];
    }
    args[argc + 1] = NULL;

    const char *paths[] = {
            "/usr/bin/strace",
            "/bin/strace",
            "/usr/local/bin/strace",
            NULL
    };

    for (int i = 0; paths[i]; i++) {
        execve(paths[i], args, environ);
    }

    perror("execve strace failed");
    exit(1);
}

// 父进程：统计系统调用耗时
void parent_process(int pfd[]) {
    close(pfd[1]);

    char buf[4096];
    char line[1024];
    int pos = 0;
    ssize_t n;

    struct timespec last, now;
    clock_gettime(CLOCK_MONOTONIC, &last);

    while (1) {
        n = read(pfd[0], buf, sizeof(buf));
        if (n <= 0) break;

        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                line[pos] = '\0';

                char name[32];
                double t;
                if (parse_strace_line(line, name, &t)) {
                    int idx = find_or_create_stat(name);
                    if (idx >= 0) stats[idx].total_time += t;
                }

                pos = 0;
            } else if (pos < sizeof(line) - 1) {
                line[pos++] = buf[i];
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec > last.tv_sec) || (now.tv_sec == last.tv_sec && now.tv_nsec - last.tv_nsec >= 1e9)) {
            print_stats();
            syscall_count = 0;
            last = now;
        }
    }

    // 最后一轮
    if (syscall_count > 0) {
        print_stats();
    }

    close(pfd[0]);
}

// 主函数
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        return 1;
    }

    int pfd[2];
    if (pipe(pfd) < 0) {
        perror("pipe");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        child_process(pfd, argc, argv);
    } else {
        parent_process(pfd);
        waitpid(pid, NULL, 0);
    }

    return 0;
}
