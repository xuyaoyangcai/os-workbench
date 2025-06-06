#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/syscall.h>
#include <regex.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>

regex_t storage;

void get_path(char paths[][512], int *len) {
    char *path = getenv("PATH");
    if (!path) {
        *len = 0;
        return;
    }

    char *copy = strdup(path);
    char *token = strtok(copy, ":");
    *len = 0;

    while (token && *len < 256) {
        strncpy(paths[*len], token, 511);
        paths[*len][511] = '\0';
        (*len)++;
        token = strtok(NULL, ":");
    }
    free(copy);
}

void match(char *line) {
    regmatch_t pmatch[2];
    if (regexec(&storage, line, 2, pmatch, 0) == 0) {
        char name[128];
        int len = pmatch[1].rm_eo - pmatch[1].rm_so;
        if (len > 127) len = 127;
        strncpy(name, line + pmatch[1].rm_so, len);
        name[len] = '\0';
        printf("%s\n", name);
    }
}

int main(int argc, char *argv[], char *env[]) {
    if (regcomp(&storage, "^([a-zA-Z0-9_]+)\\(", REG_EXTENDED) != 0) {
        perror("Failed to compile regex");
        return EXIT_FAILURE;
    }

    char paths[256][512];
    int len;
    get_path(paths, &len);

    int pipefd[2];
    if (pipe(pipefd) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) { // Child
        close(pipefd[0]);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        for (int i = 0; i < len; i++) {
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/strace", paths[i]);
            execve(full_path, argv, env);
            if (errno != ENOENT) {
                perror("execve");
                exit(EXIT_FAILURE);
            }
        }
        fprintf(stderr, "strace not found in PATH\n");
        exit(EXIT_FAILURE);
    }

    // Parent
    close(pipefd[1]);
    char buffer[4096];
    ssize_t bytes;
    char line[4096];
    size_t line_len = 0;

    while ((bytes = read(pipefd[0], buffer, sizeof(buffer))) {
        if (bytes < 0) {
            if (errno == EINTR) continue;
            perror("read");
            break;
        }

        for (ssize_t i = 0; i < bytes; i++) {
            if (buffer[i] == '\n') {
                line[line_len] = '\0';
                if (line_len > 0) match(line);
                line_len = 0;
            } else if (line_len < sizeof(line) - 1) {
                line[line_len++] = buffer[i];
            }
        }
    }

    close(pipefd[0]);
    waitpid(pid, NULL, 0);
    regfree(&storage);
    return EXIT_SUCCESS;
}