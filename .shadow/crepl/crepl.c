#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>

#define FUNC_PREFIX "__expr_wrapper_"
#define MAX_FUNC_LEN 1024
#define MAX_LINE_LEN 1024
#define MAX_GLOBAL_FUNCS_LEN 8192

int compile_and_load(const char *code, const char *func_name, void **handle) {
    FILE *fp = fopen("/tmp/tmp.c", "w");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    fprintf(fp, "#include <stdio.h>\n");
    fprintf(fp, "#include <stdlib.h>\n");
    fprintf(fp, "%s", code);
    fclose(fp);

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "gcc -fPIC -shared /tmp/tmp.c -o /tmp/tmp.so");
    if (system(cmd) != 0) {
        fprintf(stderr, "Compile failed\n");
        return -1;
    }

    *handle = dlopen("/tmp/tmp.so", RTLD_NOW);
    if (!*handle) {
        fprintf(stderr, "dlopen error: %s\n", dlerror());
        return -1;
    }

    return 0;
}

int main() {
    char line[MAX_LINE_LEN];
    char global_funcs[MAX_GLOBAL_FUNCS_LEN] = "";
    int func_counter = 0;

    printf("Welcome to crepl! Type C expressions to evaluate.\n");

    while (1) {
        printf("crepl> ");
        if (!fgets(line, sizeof(line), stdin)) break;

        if (line[0] == '\n') continue;

        // 退出命令
        if (strncmp(line, ":q", 2) == 0) break;

        // 构造函数名
        char func_name[64];
        snprintf(func_name, sizeof(func_name), FUNC_PREFIX"%d", func_counter++);

        // 构造完整代码
        char wrapper[MAX_GLOBAL_FUNCS_LEN];
        size_t used = 0;

        // 加入所有历史定义
        int written = snprintf(wrapper, sizeof(wrapper), "%s\n", global_funcs);
        if (written < 0 || written >= sizeof(wrapper)) {
            fprintf(stderr, "Code too large\n");
            continue;
        }
        used = written;

        // 构造 int func_name() { return line; }
        written = snprintf(wrapper + used, sizeof(wrapper) - used,
                           "int %s() { return %s; }\n", func_name, line);
        if (written < 0 || written >= sizeof(wrapper) - used) {
            fprintf(stderr, "Expression too long\n");
            continue;
        }

        // 尝试编译并加载
        void *handle = NULL;
        if (compile_and_load(wrapper, func_name, &handle) != 0) continue;

        // 查找函数并执行
        int (*func)() = dlsym(handle, func_name);
        if (!func) {
            fprintf(stderr, "dlsym error: %s\n", dlerror());
            dlclose(handle);
            continue;
        }

        int result = func();
        printf("= %d\n", result);

        // 将定义加入 global_funcs 以便下次使用
        char append_code[MAX_LINE_LEN + 128];
        snprintf(append_code, sizeof(append_code), "int %s() { return %s; }\n", func_name, line);

        size_t remain = sizeof(global_funcs) - strlen(global_funcs) - 1;
        if (strlen(append_code) < remain) {
            strncat(global_funcs, append_code, remain);
        } else {
            fprintf(stderr, "Too many expressions stored.\n");
        }

        dlclose(handle);
    }

    printf("Goodbye!\n");
    return 0;
}
