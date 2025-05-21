#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <ctype.h>
#include <fcntl.h>

#define MAX_LINE_LEN 1024
#define CODE_FILE "/tmp/crepl_code.c"
#define SO_FILE "/tmp/crepl_code.so"

static int expr_count = 0;

// 去除前后空白
char* strip(char* line) {
    while (isspace(*line)) line++;
    char* end = line + strlen(line) - 1;
    while (end > line && isspace(*end)) *end-- = '\0';
    return line;
}

// 写入函数或表达式包装器到 C 文件中
void write_code_to_file(const char* code, int is_expr, const char* func_name) {
    FILE* fp = fopen(CODE_FILE, "a+");
    if (!fp) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    if (is_expr) {
        fprintf(fp, "int %s() { return %s; }\n", func_name, code);
    } else {
        fprintf(fp, "%s\n", code);
    }

    fclose(fp);
}

// 编译 crepl_code.c 为共享库
int compile_code() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        char* argv[] = {
                "gcc", "-fPIC", "-shared", CODE_FILE,
                "-o", SO_FILE, NULL
        };
        execve("/usr/bin/gcc", argv, environ);
        perror("execve");
        exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
}

int main() {
    char line_buf[MAX_LINE_LEN];
    char func_name[64];
    FILE* fclear = fopen(CODE_FILE, "w");
    if (fclear) fclose(fclear);  // 清空文件

    while (1) {
        printf("crepl> ");
        fflush(stdout);

        if (!fgets(line_buf, sizeof(line_buf), stdin)) break;

        char* line = strip(line_buf);
        if (strlen(line) == 0) continue;

        int is_expr = strncmp(line, "int ", 4) != 0;

        if (is_expr) {
            snprintf(func_name, sizeof(func_name), "__expr_wrapper_%d", expr_count++);
            write_code_to_file(line, 1, func_name);
        } else {
            write_code_to_file(line, 0, NULL);
        }

        if (!compile_code()) {
            printf("Compile error.\n");
            continue;
        }

        if (is_expr) {
            void* handle = dlopen(SO_FILE, RTLD_NOW);
            if (!handle) {
                fprintf(stderr, "dlopen error: %s\n", dlerror());
                continue;
            }

            int (*expr_func)() = dlsym(handle, func_name);
            if (!expr_func) {
                fprintf(stderr, "dlsym error: %s\n", dlerror());
                dlclose(handle);
                continue;
            }

            int result = expr_func();
            printf("%d\n", result);
            dlclose(handle);
        } else {
            printf("ok\n");
        }
    }

    return 0;
}
