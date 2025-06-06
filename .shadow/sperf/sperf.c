#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

#define MAX_SYSCALLS 512
#define BUF_SIZE 4096

typedef struct {
    char name[64];
    double total_time;
} SyscallInfo;

SyscallInfo stats[MAX_SYSCALLS];
int syscall_count = 0;

double get_time_now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

int find_syscall(const char *name) {
    for (int i = 0; i < syscall_count; ++i) {
        if (strcmp(stats[i].name, name) == 0)
            return i;
    }
    if (syscall_count < MAX_SYSCALLS) {
        strncpy(stats[syscall_count].name, name, 63);
        stats[syscall_count].name[63] = '\0';
        stats[syscall_count].total_time = 0.0;
        return syscall_count++;
    }
    return -1;
}

int compare(const void *a, const void *b) {
    return ((SyscallInfo *)b)->total_time > ((SyscallInfo *)a)->total_time ? 1 : -1;
}

char *find_in_path(const char *cmd) {
    if (strchr(cmd, '/')) return strdup(cmd);
    char *path = getenv("PATH");
    if (!path) return NULL;

    char *paths = strdup(path);
    char *token = strtok(paths, ":");
    static char fullpath[1024];
    while (token) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", token, cmd);
        if (access(fullpath, X_OK) == 0) {
            free(paths);
            return strdup(fullpath);
        }
        token = strtok(NULL, ":");
    }
    free(paths);
    return NULL;
}

void parse_line(const char *line) {
    char syscall[64];
    double time;
    const char *lt = strrchr(line, '<');
    if (!lt || sscanf(lt, "<%lf>", &time) != 1) return;
    if (sscanf(line, "%63[^ (]", syscall) != 1) return;

    int idx = find_syscall(syscall);
    if (idx >= 0) stats[idx].total_time += time;
}

void print_top5() {
    double total = 0.0;
    for (int i = 0; i < syscall_count; ++i) total += stats[i].total_time;
    if (total == 0.0) return;

    qsort(stats, syscall_count, sizeof(SyscallInfo), compare);
    int top = syscall_count < 5 ? syscall_count : 5;
    for (int i = 0; i < top; ++i) {
        double percent = 100.0 * stats[i].total_time / total;
        printf("%s (%.0f%%) ", stats[i].name, percent);
    }
    // 80 nulls
    for (int i = 0; i < 80; ++i) putchar('\0');
    fflush(stdout);
    memset(stats, 0, sizeof(stats));
    syscall_count = 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        exit(1);
    }

    int pipefd[2];
    pipe(pipefd);

    pid_t child = fork();
    if (child == 0) {
        // 子进程：重定向 stderr 到 pipe，执行 strace
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        char **new_argv = malloc(sizeof(char *) * (argc + 3));
        new_argv[0] = "strace";
        new_argv[1] = "-T";
        new_argv[2] = "-e";
        new_argv[3] = "trace=all";
        new_argv[4] = "--";
        for (int i = 1; i < argc; ++i) {
            new_argv[i + 4] = argv[i];
        }
        new_argv[argc + 4] = NULL;

        char *strace_path = find_in_path("strace");
        if (!strace_path) {
            perror("strace not found in PATH");
            exit(1);
        }
        execve(strace_path, new_argv, environ);
        perror("execve failed");
        exit(1);
    }

    // 父进程：读取 strace 输出
    close(pipefd[1]);
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        perror("fdopen");
        exit(1);
    }

    setbuf(stdout, NULL);
    char buf[BUF_SIZE];
    double last_time = get_time_now();

    while (fgets(buf, sizeof(buf), fp)) {
        parse_line(buf);

        double now = get_time_now();
        if (now - last_time >= 1.0) {
            print_top5();
            last_time = now;
        }
    }

    // 输出剩余的
    print_top5();

    waitpid(child, NULL, 0);
    return 0;
}
