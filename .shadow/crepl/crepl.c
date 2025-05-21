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
#define MAX_CODE_SIZE 16384

// 去除字符串前后空白字符
char* strip(char* s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char* end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return s;
}

int safe_strcat(char *dest, size_t dest_size, const char *src) {
    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);
    if (dest_len + src_len + 1 > dest_size) {
        return -1;  // 超出容量
    }
    strcat(dest, src);
    return 0;
}

int main() {
    char line[MAX_LINE];
    char all_funcs[MAX_CODE_SIZE] = "";  // 累积函数代码
    int expr_count = 0;

    while (1) {
        printf("crepl> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            // 输入结束（Ctrl-D）
            break;
        }

        char* actual_line = strip(line);
        if (actual_line[0] == '\0') {
            // 空行忽略
            continue;
        }

        int is_expr = 0;
        char func_name[64] = {0};

        if (strncmp(actual_line, "int ", 4) == 0) {
            // 函数定义，累积到all_funcs中
            if (safe_strcat(all_funcs, sizeof(all_funcs), actual_line) != 0 ||
                safe_strcat(all_funcs, sizeof(all_funcs), "\n") != 0) {
                fprintf(stderr, "Error: accumulated code too large\n");
                exit(EXIT_FAILURE);
            }
        } else {
            // 表达式，生成包装函数名
            is_expr = 1;
            snprintf(func_name, sizeof(func_name), "__expr_wrapper_%d", expr_count++);
        }

        // 写临时C文件，包含所有函数定义 + 当前表达式包装函数（如果有）
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

        // fork + exec编译共享库
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            // 子进程执行gcc
            char *argv[] = {
                    "gcc",
                    "-fPIC",
                    "-shared",
                    CODE_FILE,
                    "-o",
                    LIB_FILE,
                    NULL
            };
            execvp("gcc", argv);
            perror("execvp");
            _exit(EXIT_FAILURE);
        } else {
            int status;
            if (waitpid(pid, &status, 0) == -1) {
                perror("waitpid");
                exit(EXIT_FAILURE);
            }
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                printf("compile error\n");
                if (is_expr) expr_count--; // 回退编号避免跳号
                continue;
            }
        }

        if (is_expr) {
            // 动态加载共享库，调用表达式包装函数
            void* handle = dlopen(LIB_FILE, RTLD_LAZY);
            if (!handle) {
                fprintf(stderr, "dlopen error: %s\n", dlerror());
                continue;
            }

            int (*func)() = (int (*)())dlsym(handle, func_name);
            if (!func) {
                fprintf(stderr, "dlsym error: %s\n", dlerror());
                dlclose(handle);
                continue;
            }

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
