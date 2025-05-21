#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

#define TMP_C_FILE "/tmp/a.c"
#define TMP_SO_FILE "/tmp/liba.so"

int main() {
    char line[4096];
    int expr_count = 0;

    while (1) {
        printf("crepl> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
            break;

        // 去除末尾换行
        line[strcspn(line, "\n")] = 0;

        FILE *fp = fopen(TMP_C_FILE, "w");
        if (!fp) {
            perror("fopen");
            exit(1);
        }

        int is_expr = 0;
        if (strncmp(line, "int ", 4) == 0) {
            fprintf(fp, "%s\n", line);
        } else {
            is_expr = 1;
            fprintf(fp, "int __expr_wrapper_%d() { return %s; }\n", expr_count++, line);
        }
        fclose(fp);

        int pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        else if (pid == 0) {
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
            exit(1);
        }
        else {
            int status;
            waitpid(pid, &status, 0);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                printf("compile error\n");
                continue;
            }

            if (is_expr) {
                void *handle = dlopen(TMP_SO_FILE, RTLD_LAZY);
                if (!handle) {
                    fprintf(stderr, "dlopen error: %s\n", dlerror());
                    continue;
                }

                char symname[64];
                snprintf(symname, sizeof(symname), "__expr_wrapper_%d", expr_count - 1);

                int (*func)() = (int (*)())dlsym(handle, symname);
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
