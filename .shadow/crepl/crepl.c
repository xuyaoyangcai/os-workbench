#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_LINE_LEN 1024
#define MAX_FUNC_LEN 8192
#define MAX_SRC_LEN 16384
#define TMP_FILE "/tmp/crepl_temp.c"
#define TMP_SO "/tmp/crepl_temp.so"

// 存储用户定义的所有函数（追加到每次代码片段中）
char global_funcs[MAX_FUNC_LEN] = "";

int compile_code(const char *code, const char *func_name) {
    // 写入临时 C 文件
    FILE *fp = fopen(TMP_FILE, "w");
    if (!fp) {
        perror("fopen");
        return -1;
    }
    fprintf(fp, "%s", code);
    fclose(fp);

    // 构造 gcc 命令，编译为共享库
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "gcc -shared -fPIC -o %s %s -Wall -Wextra -Wno-unused-result -Wno-format-truncation",
             TMP_SO, TMP_FILE);

    int status = system(cmd);
    if (status != 0) {
        fprintf(stderr, "Compilation failed\n");
        return -1;
    }

    return 0;
}

int call_func(const char *func_name) {
    void *handle = dlopen(TMP_SO, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dlopen error: %s\n", dlerror());
        return -1;
    }

    // 清除旧的错误
    dlerror();
    int (*fptr)() = (int (*)())dlsym(handle, func_name);
    char *error = dlerror();
    if (error != NULL) {
        fprintf(stderr, "dlsym error: %s\n", error);
        dlclose(handle);
        return -1;
    }

    int ret = fptr();
    printf("=> %d\n", ret);

    dlclose(handle);
    return 0;
}

int is_func_definition(const char *line) {
    return strncmp(line, "int ", 4) == 0 && strchr(line, '(') && strchr(line, ')') && strchr(line, '{');
}

int main() {
    char line[MAX_LINE_LEN];

    printf("Welcome to crepl (C REPL)\n");

    while (1) {
        printf(">>> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        // 去掉换行符
        line[strcspn(line, "\n")] = '\0';

        if (strlen(line) == 0) continue;

        if (strcmp(line, ":quit") == 0 || strcmp(line, ":q") == 0) break;

        if (is_func_definition(line)) {
            // 累加函数定义
            strncat(global_funcs, line, sizeof(global_funcs) - strlen(global_funcs) - 2);
            strncat(global_funcs, "\n", sizeof(global_funcs) - strlen(global_funcs) - 1);
            printf("[function stored]\n");
            continue;
        }

        // 为表达式包装函数
        static int counter = 0;
        char func_name[64];
        snprintf(func_name, sizeof(func_name), "__expr_wrapper_%d", counter++);

        char code[MAX_SRC_LEN] = "#include <stdio.h>\n";
        strncat(code, global_funcs, sizeof(code) - strlen(code) - 1);

        char wrapper[1024] = {0};
        snprintf(wrapper, sizeof(wrapper), "int %s() { return ", func_name);

// 剩余空间计算
        size_t used = strlen(wrapper);
        size_t remaining = sizeof(wrapper) - used - 4; // 留出空间给 `; }\n` 和终止符

        strncat(wrapper, line, remaining);
        strncat(wrapper, "; }\n", sizeof(wrapper) - strlen(wrapper) - 1);


        strncat(code, wrapper, sizeof(code) - strlen(code) - 1);

        if (compile_code(code, func_name) == 0) {
            call_func(func_name);
        }
    }

    return 0;
}
