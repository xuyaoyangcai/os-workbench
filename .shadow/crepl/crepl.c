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

// 根据当前环境获取gcc编译参数
void get_gcc_args(char *args[], int *argc) {
    // 改成指向常量字符串，避免数组溢出警告
    static const char *mflag = (sizeof(void*) == 8) ? "-m64" : "-m32";

    args[0] = "gcc";
    args[1] = (char *)mflag;
    args[2] = "-fPIC";
    args[3] = "-shared";
    args[4] = (char *)CODE_FILE;
    args[5] = "-o";
    args[6] = (char *)LIB_FILE;
    args[7] = NULL;
    *argc = 7;
}

int main() {
    char line[MAX_LINE];
    char all_funcs[16384] = "";  // 存放所有已定义函数
    int expr_count = 0;

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
            char *argv[8];
            int argc;
            get_gcc_args(argv, &argc);
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
                // 表达式计数回退，避免跳号
                if (is_expr) expr_count--;
                continue;
            }
        }

        if (is_expr) {
            // 加载共享库，调用wrapper函数
            void* handle = dlopen(LIB_FILE, RTLD_LAZY);
            if (!handle) {
                fprintf(stderr, "dlopen error: %s\n", dlerror());
                exit(EXIT_FAILURE);
            }

            int (*func)() = dlsym(handle, func_name);
            if (!func) {
                fprintf(stderr, "dlsym error: %s\n", dlerror());
                dlclose(handle);
                exit(EXIT_FAILURE);
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
