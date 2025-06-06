#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>

extern char **environ;

typedef struct {
    char name[64];
    double total_time;
    int count;
} SyscallEntry;

#define MAX_SYSCALLS 1024

SyscallEntry syscalls[MAX_SYSCALLS];
int syscall_count = 0;

double get_time_now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void parse_line(const char *line) {
    char syscall[64];
    double time = 0;

    const char *p = strrchr(line, '<');
    if (!p || sscanf(p, "<%lf>", &time) != 1) return;

    if (sscanf(line, "%63[^( \n]", syscall) != 1) return;

    // 查找是否已存在
    for (int i = 0; i < syscall_count; ++i) {
        if (strcmp(syscalls[i].name, syscall) == 0) {
            syscalls[i].total_time += time;
            syscalls[i].count += 1;
            return;
        }
    }

    // 新增
    if (syscall_count < MAX_SYSCALLS) {
        strcpy(syscalls[syscall_count].name, syscall);
        syscalls[syscall_count].total_time = time;
        syscalls[syscall_count].count = 1;
        syscall_count++;
    }
}

int compare(const void *a, const void *b) {
    double ta = ((SyscallEntry *)a)->total_time;
    double tb = ((SyscallEntry *)b)->total_time;
    return (ta < tb) ? 1 : -1;
}

void print_top5() {
    qsort(syscalls, syscall_count, sizeof(SyscallEntry), compare);

    int top = syscall_count < 5 ? syscall_count : 5;
    for (int i = 0; i < top; ++i) {
        printf("%s %.3f\n", syscalls[i].name, syscalls[i].total_time);
    }

    // 打印 80 个 '\0'
    for (int i = 0; i < 80; ++i) putchar('\0');
    fflush(stdout);
}

// 在 PATH 中查找 strace 可执行文件
char *find_in_path(const char *program) {
    static char fullpath[4096];
    char *path = getenv("PATH");
    if (!path) return NULL;

    char *p = strdup(path);
    char *dir = strtok(p, ":");
    while (dir) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, program);
        if (access(fullpath, X_OK) == 0) {
            free(p);
            return fullpath;
        }
        dir = strtok(NULL, ":");
    }
    free(p);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <program> [args...]\n", argv[0]);
        return 1;
    }

    int pipefd[2];
    pipe(pipefd);

    pid_t child = fork();
    if (child == 0) {
        // 子进程：运行 strace
        dup2(pipefd[1], 2);
        close(pipefd[0]);
        close(pipefd[1]);

        char *strace_path = find_in_path("strace");
        if (!strace_path) {
            perror("strace not found");
            exit(1);
        }

        // 构造新的 argv：strace -T -e trace=%file target args...
        char **new_argv = malloc(sizeof(char *) * (argc + 4));
        new_argv[0] = "strace";
        new_argv[1] = "-T";
        new_argv[2] = "-e";
        new_argv[3] = "trace=%file";  // optional: or remove for all syscalls
        for (int i = 1; i < argc; ++i) {
            new_argv[i + 3] = argv[i];
        }
        new_argv[argc + 3] = NULL;

        execve(strace_path, new_argv, environ);
        perror("execve");
        exit(1);
    }

    // 父进程：解析输出
    close(pipefd[1]);
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        perror("fdopen");
        return 1;
    }

    double last = get_time_now();
    char buf[4096];

    while (1) {
        if (fgets(buf, sizeof(buf), fp)) {
            parse_line(buf);
        }

        double now = get_time_now();
        if (now - last >= 1.0) {
            print_top5();
            last = now;
        }

        // 检查子进程是否退出
        int status;
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) break;

        usleep(10000);  // 降低 CPU 占用
    }

    // 最后输出一次（防止错过）
    print_top5();
    return 0;
}
