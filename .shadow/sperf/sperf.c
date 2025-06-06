#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>

#define MAX_SYSCALLS 256
#define MAX_LINE 1024

struct SyscallStat {
    char name[64];
    double time;
};

struct SyscallStat table[MAX_SYSCALLS];
int syscall_count = 0;
int pipefd[2];
int is_end = 0;
int dev_null;

// 匹配格式：stat("/etc/ld.so.cache", AT_STATX_SYNC_AS_STAT, 0x7fff..., 0, 0) = 0 <0.000006>
const char *format = "%[^<]<%lf>";

void print_stats() {
    // 输出 Top 5
    int n = syscall_count < 5 ? syscall_count : 5;
    for (int i = 0; i < n; ++i) {
        // 找最大
        int max_idx = i;
        for (int j = i + 1; j < syscall_count; ++j) {
            if (table[j].time > table[max_idx].time) {
                max_idx = j;
            }
        }
        // 交换
        if (max_idx != i) {
            struct SyscallStat tmp = table[i];
            table[i] = table[max_idx];
            table[max_idx] = tmp;
        }
        printf("%s: %.6f\n", table[i].name, table[i].time);
    }
    printf("%s\n", (char[81]){[0 ... 79] = '\0'});
    fflush(stdout);
}

void analysis(const char *line) {
    if (strncmp(line, "+++ exited", 10) == 0) {
        is_end = 1;
        return;
    }

    char name[64];
    double t;
    if (sscanf(line, format, name, &t) == 2) {
        for (int i = 0; i < syscall_count; ++i) {
            if (strcmp(name, table[i].name) == 0) {
                table[i].time += t;
                return;
            }
        }
        strcpy(table[syscall_count].name, name);
        table[syscall_count].time = t;
        syscall_count++;
    }
}

void parentProcess() {
    close(pipefd[1]);

    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        perror("fdopen failed");
        exit(1);
    }

    struct timeval last, now;
    gettimeofday(&last, NULL);

    char line[MAX_LINE];
    while (!is_end && fgets(line, sizeof(line), fp)) {
        analysis(line);
        gettimeofday(&now, NULL);
        if (now.tv_sec > last.tv_sec) {
            print_stats();
            last = now;
        }
    }
    print_stats();
    fclose(fp);
}

void childProcess(int argc, char **argv, char **envp) {
    close(pipefd[0]);
    dup2(dev_null, STDOUT_FILENO);
    dup2(dev_null, STDERR_FILENO);

    char *exec_args[64] = { "strace", "-T", "-o", NULL };
    char path_buf[64];
    snprintf(path_buf, sizeof(path_buf), "/proc/self/fd/%d", pipefd[1]);
    exec_args[3] = path_buf;

    for (int i = 1; i < argc; ++i) {
        exec_args[i + 3] = argv[i];
    }
    exec_args[argc + 3] = NULL;

    char *path = getenv("PATH");
    char *token = strtok(path, ":");
    while (token) {
        char strace_path[128];
        snprintf(strace_path, sizeof(strace_path), "%s/strace", token);
        execve(strace_path, exec_args, envp);
        token = strtok(NULL, ":");
    }

    perror("execve failed");
    exit(1);
}

int main(int argc, char **argv, char **envp) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s command [args...]\n", argv[0]);
        return 1;
    }

    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        return 1;
    }

    dev_null = open("/dev/null", O_WRONLY);
    if (dev_null < 0) {
        perror("open /dev/null failed");
        return 1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        childProcess(argc, argv, envp);
    } else {
        parentProcess();
        wait(NULL);
    }

    return 0;
}
