#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#include <regex.h>

#define MAX_SYSCALLS 512
#define LINE_BUF 1024

struct syscall_stat {
    char name[64];
    double time;
};

struct syscall_stat stats[MAX_SYSCALLS];
int stat_count = 0;

int find_syscall(const char *name) {
    for (int i = 0; i < stat_count; ++i) {
        if (strcmp(stats[i].name, name) == 0) return i;
    }
    strcpy(stats[stat_count].name, name);
    stats[stat_count].time = 0;
    return stat_count++;
}

int cmp(const void* a, const void* b) {
    double t1 = ((struct syscall_stat*)a)->time;
    double t2 = ((struct syscall_stat*)b)->time;
    return (t2 > t1) - (t2 < t1);
}

void print_top5() {
    double total_time = 0;
    for (int i = 0; i < stat_count; ++i) total_time += stats[i].time;

    qsort(stats, stat_count, sizeof(struct syscall_stat), cmp);
    int top = stat_count < 5 ? stat_count : 5;
    for (int i = 0; i < top; ++i) {
        double percent = (stats[i].time / total_time) * 100.0;
        printf("%s (%.2f%%)\n", stats[i].name, percent);
    }

    // 打印 80 个 \0 分隔
    for (int i = 0; i < 80; ++i) putchar('\0');
    fflush(stdout);
}

void parse_line(const char *line) {
    // 匹配形如：read(3, ..., ...) = 3 <0.000123>
    regex_t reg;
    regmatch_t pmatch[3];
    regcomp(&reg, "^([a-z0-9_]+).*<([0-9\\.]+)>", REG_EXTENDED);

    if (regexec(&reg, line, 3, pmatch, 0) == 0) {
        char name[64] = {0}, timebuf[32] = {0};
        int len1 = pmatch[1].rm_eo - pmatch[1].rm_so;
        int len2 = pmatch[2].rm_eo - pmatch[2].rm_so;

        strncpy(name, line + pmatch[1].rm_so, len1);
        strncpy(timebuf, line + pmatch[2].rm_so, len2);
        double t = atof(timebuf);

        int idx = find_syscall(name);
        stats[idx].time += t;
    }

    regfree(&reg);
}

int main(int argc, char *argv[], char *envp[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s command [args...]\n", argv[0]);
        exit(1);
    }

    int pipefd[2];
    pipe(pipefd); // pipefd[0] for read, [1] for write

    pid_t pid = fork();
    if (pid == 0) {
        // Child
        dup2(pipefd[1], STDERR_FILENO); // 将 stderr 重定向到 pipe 写端
        close(pipefd[0]);
        close(pipefd[1]);

        // 构造 execve 参数列表
        char **new_argv = malloc(sizeof(char*) * (argc + 3));
        new_argv[0] = "strace";
        new_argv[1] = "-T";
        for (int i = 1; i < argc; ++i) {
            new_argv[i + 1] = argv[i];
        }
        new_argv[argc + 1] = NULL;

        // 查找 strace 可执行路径
        char *paths = getenv("PATH");
        char *path_copy = strdup(paths);
        char *token = strtok(path_copy, ":");
        while (token) {
            char fullpath[512];
            snprintf(fullpath, sizeof(fullpath), "%s/strace", token);
            execve(fullpath, new_argv, envp);
            token = strtok(NULL, ":");
        }

        // 如果失败
        perror("execve failed");
        exit(1);
    } else {
        // Parent
        close(pipefd[1]);
        FILE *fp = fdopen(pipefd[0], "r");
        char line[LINE_BUF];

        struct timeval last, now;
        gettimeofday(&last, NULL);

        while (fgets(line, sizeof(line), fp)) {
            parse_line(line);

            gettimeofday(&now, NULL);
            if ((now.tv_sec > last.tv_sec) || (now.tv_sec == last.tv_sec && now.tv_usec - last.tv_usec > 1000000)) {
                print_top5();
                memset(stats, 0, sizeof(stats));
                stat_count = 0;
                last = now;
            }
        }

        fclose(fp);
        waitpid(pid, NULL, 0);
    }

    return 0;
}
