#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <ctype.h>

#define CODE_FILE "/tmp/crepl_code.c"
#define LIB_FILE  "/tmp/libcrepl.so"
#define MAX_LINE 4096

// 去除字符串前后空白
char* strip(char* s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char* end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return s;
}

// 计算数字字符串长度（十进制）
int int_len(int n) {
    int len = 0;
    if (n == 0) return 1;
    if (n < 0) { len++; n = -n; }
    while (n) {
        n /= 10;
        len++;
    }
    return len;
}

int main() {
    char line[MAX_LINE];
    char all_funcs[16384] = "";  // 存放所有已定义函数
    int expr_count = 0;

    // 从环境变量读取编译架构，默认-m64
    const char *arch = getenv("CREPL_ARCH");
    if (!arch) arch = "-m64";

    while (1) {
        printf("crepl> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        char* actual_line = strip(line);
        if (actual_line[0] == '\0') {
            continue;  // 空行跳过
        }

        int is_expr = 0;
        char func_name[64] = {0};

        if (strncmp(actual_line, "int ", 4) == 0) {
            // 函数定义，累积保存
            if (strlen(all_funcs) + strlen(actual_line) + 2 < sizeof(all_funcs)) {
                strcat(all_funcs, actual_line);
                strcat(all_funcs, "\n");
            } else {
                fprintf(stderr, "Error: too much code accumulated\n");
                exit(EXIT_FAILURE);
            }
        } else {
            // 表达式，生成wrapper函数名
            is_expr = 1;
            snprintf(func_name, sizeof(func_name), "__expr_wrapper_%d", expr_count++);
        }

        // 写入临时文件：所有已定义函数 + 当前表达式wrapper（如果有）
        FILE* fp = fopen(CODE_FILE, "w");
        if (!fp) {
            fprintf(stderr, "fopen error: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        fputs("#include <stdio.h>\n", fp);
        fputs(all_funcs, fp);

        if (is_expr) {
            fprintf(fp, "int %s() { return %s; }\n", func_name, actual_line);
        }
        fclose(fp);

        // fork + exec gcc 编译共享库
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            // 子进程执行编译
            char *argv[] = {
                    "gcc",
                    (char *)arch,
                    "-fPIC",
                    "-shared",
                    CODE_FILE,
                    "-o",
                    LIB_FILE,
                    NULL
            };
            execvp("gcc", argv);
            perror("execvp");
            exit(EXIT_FAILURE);
        } else {
            // 父进程等待编译结束
            int status;
            if (waitpid(pid, &status, 0) == -1) {
                perror("waitpid");
                exit(EXIT_FAILURE);
            }
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                printf("compile error\n");
                if (is_expr) expr_count--;
                continue;
            }
        }

        if (is_expr) {
            // 调试打印
            printf("[debug] Calling wrapper function: %s\n", func_name);
            printf("[debug] Expression: %s\n", actual_line);

            // 加载共享库，调用wrapper函数
            void* handle = dlopen(LIB_FILE, RTLD_LAZY);
            if (!handle) {
                fprintf(stderr, "dlopen error: %s\n", dlerror());
                exit(EXIT_FAILURE);
            }

            void *sym = dlsym(handle, func_name);
            if (!sym) {
                fprintf(stderr, "dlsym error: %s\n", dlerror());
                dlclose(handle);
                exit(EXIT_FAILURE);
            }

            // 安全转换函数指针
            int (*func)() = (int (*)())sym;

            int result = func();
            printf("%d\n", result);
            fflush(stdout);

            dlclose(handle);
        } else {
            printf("ok\n");
            fflush(stdout);
        }
    }

    return 0;
}
