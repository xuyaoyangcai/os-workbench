#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_SYSCALL 1024
#define BUF_SIZE 512
extern char **environ;

struct syscallStat {
    char name[64];
    double time;
};

int cmp(const void *a, const void *b) {
    double t1 = ((struct syscallStat *)a)->time;
    double t2 = ((struct syscallStat *)b)->time;
    return (t1 > t2) ? -1 : (t1 < t2);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <program> [args...]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Prepare command: strace -T program args...
    int cmd_len = argc + 3;
    char **cmdArgs = malloc(sizeof(char *) * cmd_len);
    cmdArgs[0] = "strace";
    cmdArgs[1] = "-T";
    for (int i = 1; i < argc; i++) {
        cmdArgs[i + 1] = argv[i];
    }
    cmdArgs[argc + 1] = NULL;

    // pipe for capturing strace stderr
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);
        dup2(pipefd[1], STDERR_FILENO);  // Redirect stderr to pipe
        int null_fd = open("/dev/null", O_RDWR);
        dup2(null_fd, STDOUT_FILENO);    // Redirect stdout to /dev/null
        close(null_fd);

        // Search strace in PATH
        char *path_env = getenv("PATH");
        char *path_copy = strdup(path_env);
        char *token = strtok(path_copy, ":");
        while (token) {
            char fullpath[256];
            snprintf(fullpath, sizeof(fullpath), "%s/strace", token);
            execve(fullpath, cmdArgs, environ);
            token = strtok(NULL, ":");
        }

        fprintf(stderr, "strace not found in PATH\n");
        exit(127);
    }

    // Parent process
    close(pipefd[1]);
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        perror("fdopen");
        exit(EXIT_FAILURE);
    }

    struct syscallStat stats[MAX_SYSCALL];
    int stat_count = 0;
    double total_time = 0;

    struct timeval last_time, current_time;
    gettimeofday(&last_time, NULL);

    char buf[BUF_SIZE];
    while (fgets(buf, sizeof(buf), fp)) {
        char syscall[64];
        double usec;

        if (sscanf(buf, "%[^<]<%lf>", syscall, &usec) == 2) {
            // Trim syscall name to exclude parameters
            for (int i = 0; syscall[i]; i++) {
                if (syscall[i] == '(') {
                    syscall[i] = '\0';
                    break;
                }
            }

            // Accumulate stats
            int found = 0;
            for (int i = 0; i < stat_count; i++) {
                if (strcmp(stats[i].name, syscall) == 0) {
                    stats[i].time += usec;
                    found = 1;
                    break;
                }
            }

            if (!found && stat_count < MAX_SYSCALL) {
                strncpy(stats[stat_count].name, syscall, sizeof(stats[stat_count].name) - 1);
                stats[stat_count].time = usec;
                stat_count++;
            }

            total_time += usec;
        }

        // Check time
        gettimeofday(&current_time, NULL);
        if (current_time.tv_sec != last_time.tv_sec) {
            qsort(stats, stat_count, sizeof(struct syscallStat), cmp);
            for (int i = 0; i < 5 && i < stat_count; i++) {
                int percent = (int)((stats[i].time / total_time) * 100);
                printf("%s (%d%%)\n", stats[i].name, percent);
            }
            printf("==================\n");
            for (int i = 0; i < 80; i++) putchar('\0');
            fflush(stdout);
            gettimeofday(&last_time, NULL);
        }
    }

    // Final output
    qsort(stats, stat_count, sizeof(struct syscallStat), cmp);
    for (int i = 0; i < 5 && i < stat_count; i++) {
        int percent = (int)((stats[i].time / total_time) * 100);
        printf("%s (%d%%)\n", stats[i].name, percent);
    }
    printf("==================\n");
    for (int i = 0; i < 80; i++) putchar('\0');
    fflush(stdout);

    wait(NULL); // Wait for child
    free(cmdArgs);
    return 0;
}
