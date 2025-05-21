// crepl.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/wait.h>

#define MAX_SRC_LEN 8192

int expr_counter = 0;
char global_funcs[MAX_SRC_LEN] = "";  // 用于存储所有历史定义的函数

// 写入源代码文件
void write_source_file(const char *filename, const char *code) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("fopen");
        exit(1);
    }
    fprintf(fp, "%s\n", code);
    fclose(fp);
}

// 编译为 .so 动态库
void compile_shared_object(const char *src, const char *so) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("gcc", "gcc", "-fPIC", "-shared", "-o", so, src, NULL);
        perror("execlp");
        exit(1);
    }
    int status;
    waitpid(pid, &status, 0);
    if (status != 0) {
        fprintf(stderr, "Compilation failed.\n");
    }
}

// 执行表达式函数
void run_expr_function(const char *so_path, const char *func_name) {
    void *handle = dlopen(so_path, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dlopen error: %s\n", dlerror());
        return;
    }

    int (*func)() = dlsym(handle, func_name);
    if (!func) {
        fprintf(stderr, "dlsym error: %s\n", dlerror());
        dlclose(handle);
        return;
    }

    int result = func();
    printf("= %d\n", result);
    dlclose(handle);
}

int main() {
    char line[1024];

    printf("crepl> ");
    while (fgets(line, sizeof(line), stdin)) {
        if (strncmp(line, "int ", 4) == 0) {
            // 函数定义
            strncat(global_funcs, line, sizeof(global_funcs) - strlen(global_funcs) - 1);
            printf("OK.\n");
        } else {
            // 表达式处理
            expr_counter++;
            char func_name[64];
            sprintf(func_name, "__expr_wrapper_%d", expr_counter);

            char code[MAX_SRC_LEN];
            snprintf(code, sizeof(code),
                     "#include <stdio.h>\n"
                     "%s\n" // 所有已定义的函数
                     "int %s() { return %s; }\n",
                     global_funcs, func_name, line);

            // 创建源文件和 .so 文件路径
            char src_path[] = "/tmp/crepl_src_XXXXXX.c";
            int src_fd = mkstemps(src_path, 2);  // 后缀 .c
            if (src_fd == -1) {
                perror("mkstemps");
                continue;
            }
            close(src_fd);  // 会用 fopen 写入

            char so_path[256];
            snprintf(so_path, sizeof(so_path), "%.*s.so", (int)(strlen(src_path) - 2), src_path);

            write_source_file(src_path, code);
            compile_shared_object(src_path, so_path);
            run_expr_function(so_path, func_name);

            unlink(src_path);
            unlink(so_path);
        }

        printf("crepl> ");
    }

    return 0;
}
