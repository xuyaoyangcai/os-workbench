#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <regex.h>
#include <string.h>
#include <time.h>
#include <errno.h>

volatile sig_atomic_t done = 0;

void handler(int sig) {
    done = 1;
}

char paths[256][512];
int path_count;
regex_t reg;

void get_path() {
    char *path = getenv("PATH");
    if (!path) return;
    int i = 0;
    char *start = path;
    for (char *p = path; ; p++) {
        if (*p == ':' || *p == '\0') {
            int len = p - start;
            if (len > 0 && len < 512) {
                strncpy(paths[i], start, len);
                paths[i][len] = 0;
                i++;
            }
            if (*p == '\0') break;
            start = p + 1;
        }
    }
    path_count = i;
}

void match_and_print(char *line) {
    regmatch_t pmatch[2];
    if (regexec(&reg, line, 2, pmatch, 0) == 0) {
        int start = pmatch[1].rm_so;
        int end = pmatch[1].rm_eo;
        if (end > start && end - start < 4096) {
            char syscall[4096];
            strncpy(syscall, line + start, end - start);
            syscall[end - start] = '\0';
            printf("%s\n", syscall);
        }
    }
}

int main(int argc, char *argv[], char *envp[]) {
    signal(SIGCHLD, handler);
    regcomp(&reg, "^(.*?)\\(.*?\\) += .*?$", REG_EXTENDED);
    get_path();

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        exit(1);
    }

    pid_t pid = fork();
    if (pid == 0) {
        // 子进程
        close(pipefd[0]); // 关闭读端
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // 构造 exec 参数
        char **exec_argv;
        int real_argc = argc > 1 ? argc - 1 : 1;

        exec_argv = malloc(sizeof(char*) * (real_argc + 4));
        exec_argv[0] = "strace";
        exec_argv[1] = "-T";
        exec_argv[2] = "-o/dev/stderr";

        if (argc > 1) {
            for (int i = 1; i < argc; i++) {
                exec_argv[i + 2] = argv[i];
            }
        } else {
            exec_argv[3] = "true";
        }
        exec_argv[real_argc + 3] = NULL;

        for (int i = 0; i < path_count; i++) {
            char pathbuf[1024];
            snprintf(pathbuf, sizeof(pathbuf), "%s/strace", paths[i]);
            execve(pathbuf, exec_argv, envp);
        }

        perror("execve");
        exit(1);
    }

    // 父进程
    close(pipefd[1]); // 关闭写端
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        perror("fdopen");
        exit(1);
    }

    char *line = NULL;
    size_t n = 0;
    while (!done) {
        ssize_t r = getline(&line, &n, fp);
        if (r > 0) {
            match_and_print(line);
        } else {
            if (feof(fp)) break;
        }
    }

    fclose(fp);
    if (line) free(line);
    regfree(&reg);
    waitpid(pid, NULL, 0);
    return 0;
}
