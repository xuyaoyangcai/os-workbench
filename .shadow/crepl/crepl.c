#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>

#define WRAP_PREFIX "__expr_wrapper"
#define CODE_FILE "/tmp/crepl_code.c"
#define SO_FILE "/tmp/crepl_code.so"

int compile_and_load(const char *expr, void **handle, char *func_name) {
    FILE *fp = fopen(CODE_FILE, "w");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    snprintf(func_name, 64, "%s", WRAP_PREFIX);

    fprintf(fp,
            "#include <stdio.h>\n"
            "int %s() { return %s; }\n",
            func_name, expr
    );
    fclose(fp);

    if (system("gcc -fPIC -shared -o " SO_FILE " " CODE_FILE) != 0) {
        fprintf(stderr, "Compile error.\n");
        return -1;
    }

    *handle = dlopen(SO_FILE, RTLD_NOW);
    if (!*handle) {
        fprintf(stderr, "dlopen error: %s\n", dlerror());
        return -1;
    }

    return 0;
}

int main() {
    char line[1024];
    printf("Welcome to crepl!\n");

    while (1) {
        printf("crepl> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        if (strncmp(line, ":q", 2) == 0) break;

        char func_name[64];
        void *handle = NULL;

        if (compile_and_load(line, &handle, func_name) != 0) continue;

        int (*func)() = dlsym(handle, func_name);
        if (!func) {
            fprintf(stderr, "dlsym error: %s\n", dlerror());
            dlclose(handle);
            continue;
        }

        int result = func();
        printf("= %d\n", result);
        dlclose(handle);
    }

    printf("Goodbye!\n");
    return 0;
}
