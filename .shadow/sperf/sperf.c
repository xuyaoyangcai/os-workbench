#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#define zassert(x, s) \
    do { if (!(x)) { fprintf(stderr, "%s\n", s); assert(x); } } while (0)

char buf[BUFSIZ];

struct info {
    char name[100];
    double time;
    struct info *next;
};

struct info *syscall_info = NULL;

// 添加或更新系统调用信息
void add(double time, const char *name) {
    for (struct info *i = syscall_info; i != NULL; i = i->next) {
        if (strcmp(i->name, name) == 0) {
            i->time += time;
            return;
        }
    }
    struct info *si = (struct info *)malloc(sizeof(struct info));
    zassert(si != NULL, "malloc failed");
    strcpy(si->name, name);
    si->time = time;
    si->next = syscall_info;
    syscall_info = si;
}

int main(int argc, char *argv[], char *envp[]) {
    zassert(argc >= 2, "need at least one argument");

    // 构造strace参数数组，跟踪第一个参数的命令
    char *strace_argv[] = { "strace", "-r", argv[1], NULL };

    int pipefd[2];
    zassert(pipe(pipefd) == 0, "create pipe failed");

    pid_t pid = fork();
    zassert(pid >= 0, "fork failed");

    if (pid == 0) {
        // 子进程：重定向stderr到管道写端
        close(pipefd[0]);          // 关闭管道读端
        dup2(pipefd[1], STDERR_FILENO); // stderr -> 管道写端
        close(pipefd[1]);

        // 关闭stdout，防止命令的标准输出干扰
        close(STDOUT_FILENO);

        execve("/bin/strace", strace_argv, envp);

        perror("execve failed");
        exit(EXIT_FAILURE);
    } else {
        // 父进程：关闭管道写端，读取子进程输出
        close(pipefd[1]);
        FILE *fp = fdopen(pipefd[0], "r");
        zassert(fp != NULL, "fdopen failed");

        double time;
        char name[100];

        while (fgets(buf, sizeof(buf), fp)) {
            // strace -r 格式示例:
            // 0.000012 read(3, "", 0) = 0
            // 1.2345678 openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
            // sscanf按格式读第一个double和一个字符串
            if (sscanf(buf, "%lf %99s", &time, name) != 2) {
                continue; // 解析失败跳过
            }

            if (name[0] == '+') { // 忽略类似 "+ ..." 的行
                continue;
            }

            // 去掉name中的参数部分，比如 openat(...) -> openat
            for (int i = 0; name[i]; i++) {
                if (name[i] == '(') {
                    name[i] = '\0';
                    break;
                }
            }

            add(time, name);
        }

        fclose(fp);

        // 统计总时间和排序
        double total = 0.0;
        struct info syscall_sort[5] = {0};

        for (struct info *i = syscall_info; i != NULL; i = i->next) {
            total += i->time;
            // 插入排序维护top5耗时最大调用
            for (int j = 0; j < 5; j++) {
                if (syscall_sort[j].time < i->time) {
                    for (int k = 4; k > j; k--) {
                        syscall_sort[k] = syscall_sort[k - 1];
                    }
                    syscall_sort[j] = *i;
                    break;
                }
            }
        }

        if (total == 0) {
            printf("No syscall time recorded.\n");
        } else {
            double others = total;
            printf("Top 5 syscalls by time spent:\n");
            for (int i = 0; i < 5; i++) {
                if (syscall_sort[i].time > 0) {
                    double percent = syscall_sort[i].time / total * 100;
                    printf("%-20s : %.6f s (%.2f%%)\n", syscall_sort[i].name, syscall_sort[i].time, percent);
                    others -= syscall_sort[i].time;
                }
            }
            if (others > 0.000001) {
                printf("%-20s : %.6f s (%.2f%%)\n", "others", others, others / total * 100);
            }
            printf("Total time: %.6f s\n", total);
        }

        // 释放链表内存
        while (syscall_info) {
            struct info *tmp = syscall_info;
            syscall_info = syscall_info->next;
            free(tmp);
        }
    }

    return 0;
}
