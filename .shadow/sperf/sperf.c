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

typedef struct
{
    char name[32];
    double time;
} SyscallInfo;

typedef struct
{
    char name[32];
    double total_time;
    double percentage;
} SyscallStat;

#define MAX_SYSCALLS 1024
#define TOP_N 5

SyscallInfo syscalls[MAX_SYSCALLS];
SyscallStat stats[MAX_SYSCALLS];
int syscall_count = 0;

// 比较函数用于排序
int compare_syscall_stats(const void *a, const void *b)
{
    const SyscallStat *sa = (const SyscallStat *)a;
    const SyscallStat *sb = (const SyscallStat *)b;
    if (sa->total_time > sb->total_time)
        return -1;
    if (sa->total_time < sb->total_time)
        return 1;
    return 0;
}

// 解析一行 strace 输出，提取系统调用名称和时间
int parse_strace_line(char *line, char *name, double *time)
{
    char *time_start = strstr(line, "<");
    char *time_end = strstr(line, ">");

    if (!time_start || !time_end)
        return 0;

    // 提取系统调用名
    char *name_start = line;
    char *name_end = strchr(line, '(');

    if (!name_end)
        return 0;

    int name_len = name_end - name_start;
    if (name_len >= 32 || name_len <= 0)
        return 0;

    strncpy(name, name_start, name_len);
    name[name_len] = '\0';

    // 提取时间
    time_start++; // 跳过 '<'
    *time = atof(time_start);

    return 1;
}

// 查找或创建系统调用统计信息
int find_or_create_stat(const char *name)
{
    for (int i = 0; i < syscall_count; i++)
    {
        if (strcmp(stats[i].name, name) == 0)
        {
            return i;
        }
    }

    if (syscall_count < MAX_SYSCALLS)
    {
        strcpy(stats[syscall_count].name, name);
        stats[syscall_count].total_time = 0;
        stats[syscall_count].percentage = 0;
        return syscall_count++;
    }

    return -1; // 数组已满
}

// 输出统计信息
void print_stats()
{
    double total_time = 0;

    // 计算总时间
    for (int i = 0; i < syscall_count; i++)
    {
        total_time += stats[i].total_time;
    }

    // 计算百分比
    for (int i = 0; i < syscall_count; i++)
    {
        stats[i].percentage = (total_time > 0) ? (stats[i].total_time * 100 / total_time) : 0;
    }

    // 排序
    qsort(stats, syscall_count, sizeof(SyscallStat), compare_syscall_stats);

    // 输出前 TOP_N 个
    int limit = (syscall_count < TOP_N) ? syscall_count : TOP_N;
    for (int i = 0; i < limit; i++)
    {
        printf("%s (%d%%)\n", stats[i].name, (int)(stats[i].percentage + 0.5));
    }

    // 输出 80 个 '\0' 作为分隔符
    for (int i = 0; i < 80; i++)
    {
        putchar('\0');
    }
    fflush(stdout);
}

void child_process(int pfd[], int argc, char *argv[])
{
    close(pfd[0]); // 关闭读端

    // 重定向标准错误到管道
    dup2(pfd[1], STDERR_FILENO);
    close(pfd[1]);

    // 构建 strace 命令行参数
    char **strace_args = (char **)malloc((argc + 3) * sizeof(char *));
    strace_args[0] = "strace";
    strace_args[1] = "-T"; // 显示系统调用时间

    // 复制用户命令及参数
    for (int i = 1; i < argc; i++)
    {
        strace_args[i + 1] = argv[i];
    }
    strace_args[argc + 2] = NULL;

    // 尝试执行 strace，查找可能的路径
    char *paths[] = {
            "/bin/strace",
            "/usr/bin/strace",
            "/usr/local/bin/strace",
            NULL};

    for (char **path = paths; *path; path++)
    {
        execve(*path, strace_args, environ);
    }

    // 如果执行到这里，说明 execve 都失败了
    perror("execve strace");
    exit(1);
}

void parent_process(int pfd[])
{
    close(pfd[1]); // 关闭写端

    char buffer[4096];
    char line[1024] = {0};
    int line_pos = 0;

    // 读取 strace 输出
    while (1)
    {
        // 检查子进程是否结束
        int status;
        pid_t w = waitpid(-1, &status, WNOHANG);
        if (w > 0)
        {
            // 子进程已结束，打印最后的统计信息
            print_stats();
            break;
        }

        // 尝试从管道读取数据
        int n = read(pfd[0], buffer, sizeof(buffer) - 1);
        if (n > 0)
        {
            buffer[n] = '\0';

            // 处理读取到的数据
            for (int i = 0; i < n; i++)
            {
                if (buffer[i] == '\n')
                {
                    // 行结束，解析该行
                    line[line_pos] = '\0';

                    char syscall_name[32];
                    double syscall_time;

                    if (parse_strace_line(line, syscall_name, &syscall_time))
                    {
                        int idx = find_or_create_stat(syscall_name);
                        if (idx >= 0)
                        {
                            stats[idx].total_time += syscall_time;
                        }
                    }

                    line_pos = 0;
                }
                else if (line_pos < sizeof(line) - 1)
                {
                    // 继续构建当前行
                    line[line_pos++] = buffer[i];
                }
            }
        }

        usleep(1000); // 减少休眠时间，提高响应速度
    }

    close(pfd[0]);
}

int main(int argc, char *argv[])
{
    // 检查参数
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        return 1;
    }

    // 创建管道
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        perror("pipe");
        return 1;
    }

    // 创建子进程
    pid_t pid = fork();
    if (pid == -1)
    {
        perror("fork");
        return 1;
    }

    if (pid == 0)
    {
        // 子进程
        child_process(pipefd, argc, argv);
    }
    else
    {
        // 父进程
        parent_process(pipefd);
    }

    return 0;
}