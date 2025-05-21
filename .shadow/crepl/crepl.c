#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dlfcn.h>

#define TMP_C_FILE "/tmp/a.c"
#define TMP_SO_FILE "/tmp/liba.so"
#define PROMPT "crepl> "

// 去除字符串前后空白
static char *strip(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    return s;
}

int main(void) {
    char line[4096];
    int expr_count = 0;

    while (1) {
        printf(PROMPT);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            // EOF 或错误退出
            break;
        }

        char *actual_line = strip(line);
        if (actual_line[0] == '\0') {
            // 空行忽略
            continue;
        }

        int is_expr = 0;

        // 写入临时 C 文件
        FILE *fp = fopen(TMP_C_FILE, "w");
        if (!fp) {
            fprintf(stderr, "fopen error: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (strncmp(actual_line, "int ", 4) == 0) {
            // 函数定义，直接写入
            fprintf(fp, "%s\n", actual_line);
        } else {
            // 表达式，生成包装函数
            is_expr = 1;
            fprintf(fp, "int __expr_wrapper_%d() { return %s; }\n", expr_count++, actual_line);
        }
        fclose(fp);

        // fork + exec 编译
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "fork error: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            // 子进程调用 gcc 生成动态库
            char *argv[] = {
                    "gcc",
                    "-fPIC",
                    "-shared",
                    TMP_C_FILE,
                    "-o",
                    TMP_SO_FILE,
                    NULL
            };
            execve("/usr/bin/gcc", argv, NULL);
            perror("execve");
            _exit(EXIT_FAILURE);
        } else {
            // 父进程等待
            int status;
            if (waitpid(pid, &status, 0) == -1) {
                fprintf(stderr, "waitpid error: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                printf("compile error\n");
                continue;
            }

            if (is_expr) {
                // 加载动态库，调用表达式包装函数
                void *handle = dlopen(TMP_SO_FILE, RTLD_LAZY);
                if (!handle) {
                    fprintf(stderr, "dlopen error: %s\n", dlerror());
                    continue;
                }

                char func_name[64];
                snprintf(func_name, sizeof(func_name), "__expr_wrapper_%d", expr_count - 1);

                // 函数指针转换兼容32/64位
                int (*func)() = (int (*)())dlsym(handle, func_name);
                if (!func) {
                    fprintf(stderr, "dlsym error: %s\n", dlerror());
                    dlclose(handle);
                    continue;
                }

                int ret = func();
                printf("%d\n", ret);

                dlclose(handle);
            } else {
                printf("ok\n");
            }
        }
    }
    return 0;
}
