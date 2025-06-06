#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <regex.h>
#include <sys/select.h>
#include <signal.h>

#define NODE_NUM 1000
#define MAX_LEN 512

typedef struct node {
    char name[MAX_LEN];
    double time;
} Node;

Node nodes[NODE_NUM];

// 读一行，带超时(100ms)，成功返回1，失败或超时返回0
int readline(int fd, char* str, int len) {
    int i = 0;
    memset(str, '\0', len);
    while (i < len - 1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);
        struct timeval timeout = {0, 100000}; // 0.1秒

        int ret = select(fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ret == -1) {
            perror("select error");
            return 0;
        } else if (ret == 0) {
            // 超时
            return 0;
        } else {
            char c;
            ssize_t n = read(fd, &c, 1);
            if (n == 1) {
                if (c == '\n') {
                    str[i] = '\0';
                    return 1;
                }
                str[i++] = c;
            } else if (n == 0) {
                // EOF
                return 0;
            } else {
                perror("read error");
                return 0;
            }
        }
    }
    // 超长行，截断返回
    str[i] = '\0';
    return 1;
}

int cmp(const void* a, const void* b) {
    double diff = ((Node*)b)->time - ((Node*)a)->time;
    if (diff > 0)
        return 1;
    else if (diff < 0)
        return -1;
    else
        return 0;
}

void wait_forever() {
    sigset_t mask;
    sigemptyset(&mask);
    while (1) {
        sigsuspend(&mask);
    }
}

int main(int argc, char* argv[], char* exec_envp[]) {
    if (argc == 1) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        wait_forever();
    }

    for (int i = 0; i < NODE_NUM; i++) {
        memset(nodes[i].name, '\0', MAX_LEN);
        nodes[i].time = 0;
    }

    // 构造exec参数: strace -T command args...
    char** exec_argv = malloc(sizeof(char*) * (argc + 2));
    exec_argv[0] = "strace";
    exec_argv[1] = "-T";
    for (int i = 1; i < argc; i++) {
        exec_argv[i + 1] = argv[i];
    }
    exec_argv[argc + 1] = NULL;

    // 查找strace路径，优先从PATH最后开始找
    char* paths = getenv("PATH");
    if (!paths) {
        fprintf(stderr, "PATH environment variable not found\n");
        exit(EXIT_FAILURE);
    }
    char pathcopy[MAX_LEN];
    strncpy(pathcopy, paths, sizeof(pathcopy) - 1);
    pathcopy[sizeof(pathcopy) - 1] = '\0';

    char* path = strtok(pathcopy, ":");
    char strace_paths[50][MAX_LEN];
    int strace_num = 0;
    while (path != NULL) {
        int len = strlen(path);
        snprintf(strace_paths[strace_num], MAX_LEN, "%s/strace", path);
        strace_num++;
        path = strtok(NULL, ":");
    }

    regex_t reg;
    if (regcomp(&reg, "(\\w+)\\([^)]*\\)\\s*=.+<([0-9]+\\.[0-9]+)>", REG_EXTENDED) != 0) {
        perror("REGEX COMPILE ERROR");
        exit(EXIT_FAILURE);
    }

    int fildes[2];
    if (pipe(fildes) != 0) {
        perror("PIPE creation failed");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        close(fildes[0]); // 关闭读端
        dup2(fildes[1], STDERR_FILENO);
        close(fildes[1]);

        int fdnull = open("/dev/null", O_RDWR);
        if (fdnull < 0) {
            perror("open /dev/null");
            exit(EXIT_FAILURE);
        }
        dup2(fdnull, STDOUT_FILENO);
        close(fdnull);

        // execve从后往前尝试找到有效的strace路径
        for (int i = strace_num - 1; i >= 0; i--) {
            execve(strace_paths[i], exec_argv, exec_envp);
        }
        perror("execve strace failed");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        // Parent process
        close(fildes[1]); // 关闭写端
        char buf[MAX_LEN];
        int node_count = 0;
        double total_time = 0.0;
        clock_t last_time = clock();

        while (readline(fildes[0], buf, MAX_LEN)) {
            regmatch_t matches[3];
            if (regexec(&reg, buf, 3, matches, 0) == 0) {
                char name[MAX_LEN] = {0};
                char time_str[MAX_LEN] = {0};

                int name_len = matches[1].rm_eo - matches[1].rm_so;
                int time_len = matches[2].rm_eo - matches[2].rm_so;
                strncpy(name, buf + matches[1].rm_so, name_len);
                name[name_len] = '\0';
                strncpy(time_str, buf + matches[2].rm_so, time_len);
                time_str[time_len] = '\0';

                double t = atof(time_str);

                int found = 0;
                for (int i = 0; i < node_count; i++) {
                    if (strcmp(nodes[i].name, name) == 0) {
                        nodes[i].time += t;
                        found = 1;
                        break;
                    }
                }
                if (!found && node_count < NODE_NUM) {
                    strncpy(nodes[node_count].name, name, MAX_LEN - 1);
                    nodes[node_count].time = t;
                    node_count++;
                }
                total_time += t;

                clock_t now = clock();
                if ((now - last_time) * 1000 / CLOCKS_PER_SEC >= 2000) {
                    qsort(nodes, node_count, sizeof(Node), cmp);

                    printf("Total: %.6fs\n", total_time);
                    for (int i = 0; i < node_count && i < 5; i++) {
                        int ratio = (int)(nodes[i].time / total_time * 100 + 0.5);
                        printf("%s (%d%%)\n", nodes[i].name, ratio);
                    }
                    for (int i = 0; i < 80; i++) {
                        putchar('\0');
                    }
                    printf("\n====================\n");
                    fflush(stdout);

                    last_time = now;
                }
            } else if (strstr(buf, "exited with") != NULL) {
                break;
            }
        }

        close(fildes[0]);
        regfree(&reg);
        free(exec_argv);
        wait(NULL);
    } else {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    return 0;
}
