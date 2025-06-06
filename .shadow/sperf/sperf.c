#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

extern char **environ;

struct syscallNameAndTime {
    char name[50];
    double time;
};

int cmp(const void *s1, const void *s2) {
    return (*(struct syscallNameAndTime *)s1).time > (*(struct syscallNameAndTime *)s2).time ? -1 : 1;
}

int myReadLine(int fd, char *line) {
    char ch;
    int offset = 0;
    while (read(fd, &ch, 1) > 0) {
        if (offset < 511) line[offset++] = ch;
        if (ch == '\n') {
            line[offset - 1] = '\0';
            return 1;
        }
    }
    line[offset] = '\0';
    return offset > 0 ? 1 : -1;
}

int main(int argc, char *argv[]) {
    struct syscallNameAndTime syscallList[1000];
    for (int i = 0; i < 1000; i++) {
        strcpy(syscallList[i].name, "NONE");
        syscallList[i].time = 0;
    }

    // Prepare strace command
    int realArgc = argc > 1 ? argc - 1 : 1;
    char **cmdArgs = malloc(sizeof(char *) * (realArgc + 3));
    if (!cmdArgs) {
        perror("malloc cmdArgs");
        exit(1);
    }

    cmdArgs[0] = "strace";
    cmdArgs[1] = "-T";
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            cmdArgs[i + 1] = argv[i];
        }
    } else {
        cmdArgs[2] = "true";  // fallback command
    }
    cmdArgs[realArgc + 2] = NULL;

    // PATH env
    char PATH[1024];
    char *pathvar = getenv("PATH");
    if (!pathvar) pathvar = "/usr/bin:/bin";
    snprintf(PATH, sizeof(PATH), "%s", pathvar);

    int pipefds[2];
    if (pipe(pipefds) < 0) {
        perror("pipe");
        exit(1);
    }

    pid_t pid = fork();
    if (pid == 0) {
        // child
        close(pipefds[0]);
        dup2(pipefds[1], STDERR_FILENO);
        int fd = open("/dev/null", O_RDWR);
        dup2(fd, STDOUT_FILENO);

        char *token = strtok(PATH, ":");
        char stracePath[256];
        while (token) {
            snprintf(stracePath, sizeof(stracePath), "%s/strace", token);
            execve(stracePath, cmdArgs, environ);
            token = strtok(NULL, ":");
        }

        perror("execve strace failed");
        exit(1);
    } else {
        // parent
        close(pipefds[1]);
        char buf[512];
        int listLen = 0;
        double totalTime = 0;
        int pre = clock();

        while (myReadLine(pipefds[0], buf) > 0) {
            int now = clock();
            if (now - pre >= CLOCKS_PER_SEC) {
                qsort(syscallList, listLen, sizeof(struct syscallNameAndTime), cmp);
                for (int i = 0; i < 5 && i < listLen; i++) {
                    printf("%s (%d%%)\n", syscallList[i].name,
                           (int)((syscallList[i].time / totalTime) * 100));
                }
                printf("==================\n");
                for (int i = 0; i < 80; i++) {
                    putchar('\0');
                }
                fflush(stdout);
                pre = now;
            }

            int len = strlen(buf);
            if (len < 3) continue;

            int left = -1, right = len - 1, leftParam = -1;
            for (int i = len - 1; i >= 0; i--) {
                if (buf[i] == '<') {
                    left = i;
                    break;
                }
            }
            for (int i = 0; i < len; i++) {
                if (buf[i] == '(') {
                    leftParam = i;
                    break;
                }
            }

            if (buf[right] == '>' && left >= 0 && leftParam > 0 &&
                ('a' <= buf[0] && buf[0] <= 'z')) {
                char syscall[50] = {0};
                char timeStr[100] = {0};
                memcpy(syscall, buf, leftParam < 50 ? leftParam : 49);
                memcpy(timeStr, buf + left + 1, (right - left - 1) < 99 ? (right - left - 1) : 99);
                double dtime = strtod(timeStr, NULL);
                for (int i = 0; i <= listLen; i++) {
                    if (strcmp(syscallList[i].name, "NONE") == 0) {
                        strcpy(syscallList[i].name, syscall);
                        syscallList[i].time = dtime;
                        totalTime += dtime;
                        listLen++;
                        break;
                    } else if (strcmp(syscallList[i].name, syscall) == 0) {
                        syscallList[i].time += dtime;
                        totalTime += dtime;
                        break;
                    }
                }
            }
        }

        // Final output
        qsort(syscallList, listLen, sizeof(struct syscallNameAndTime), cmp);
        for (int i = 0; i < 5 && i < listLen; i++) {
            printf("%s (%d%%)\n", syscallList[i].name,
                   (int)((syscallList[i].time / totalTime) * 100));
        }
        printf("==================\n");
        for (int i = 0; i < 80; i++) putchar('\0');
        fflush(stdout);

        free(cmdArgs);
        return 0;
    }
}
