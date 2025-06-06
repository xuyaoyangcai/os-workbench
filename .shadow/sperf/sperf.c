#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <assert.h>

#define regular "%[^(](%*[^)]) = %*[^ ] %fs"
#define MAX_ENTRIES 1024

typedef struct {
    char name[128];
    float time;
} syscall_entry;

int Pipe[2];
int *readPort, *writePort;
int devNull;
char PATH[1024];
const char *delim = ":";
syscall_entry total_table[MAX_ENTRIES];
syscall_entry current_table[MAX_ENTRIES];
int total_count = 0;
int current_count = 0;
int isEnd = 0;

void Assert(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "Assertion failed: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

void update_table(syscall_entry *table, int *count, char *name, float time) {
    for (int i = 0; i < *count; i++) {
        if (strcmp(table[i].name, name) == 0) {
            table[i].time += time;
            return;
        }
    }
    if (*count < MAX_ENTRIES) {
        strcpy(table[*count].name, name);
        table[*count].time = time;
        (*count)++;
    }
}

void print_table(syscall_entry *table, int count) {
    for (int i = 0; i < count; i++) {
        printf("%s: %.6f\n", table[i].name, table[i].time);
    }
    printf("\n");
    fflush(stdout);
}

void analysis(char buffer[], ssize_t count) {
    char name[128];
    float time = 0.0f;

    if (buffer[0] == '+') {
        isEnd = 1;
        return;
    }

    if (sscanf(buffer, regular, name, &time) == 2) {
        update_table(total_table, &total_count, name, time);
        update_table(current_table, &current_count, name, time);
    }
}

void subProcessing(int argc, char *argv[], char *envp[]) {
    dup2(devNull, fileno(stdout));
    dup2(devNull, fileno(stderr));
    close(*readPort);

    char *exec_argv[32] = {"strace", "-T", "-o"};
    char writeFile[32];
    sprintf(writeFile, "/proc/self/fd/%d", *writePort);
    exec_argv[3] = writeFile;

    for (int i = 1; i < argc; i++) {
        exec_argv[i + 3] = argv[i];
    }
    exec_argv[argc + 3] = NULL;

    char *token = strtok(PATH, delim);
    while (token != NULL) {
        char straceFile[128];
        sprintf(straceFile, "%s/strace", token);
        execve(straceFile, exec_argv, envp);
        token = strtok(NULL, delim);
    }

    perror("execve failed");
    exit(EXIT_FAILURE);
}

void parentProcessing() {
    close(*writePort);
    char buf[2], arr[1024];
    int index = 0;
    struct timeval start, end;
    gettimeofday(&start, NULL);

    while (1) {
        if (isEnd) {
            print_table(total_table, total_count);
            break;
        }

        while (read(*readPort, buf, 1) > 0) {
            arr[index++] = buf[0];
            if (buf[0] == '\n') {
                arr[index] = '\0';
                analysis(arr, index);
                index = 0;
                break;
            }
        }

        gettimeofday(&end, NULL);
        if (end.tv_sec - start.tv_sec >= 1) {
            print_table(current_table, current_count);
            current_count = 0;
            start = end;
        }
    }
    close(*readPort);
}

void forkRead(int argc, char *argv[], char *envp[]) {
    pid_t cpid = fork();
    Assert(cpid != -1, "fork failed");

    if (cpid == 0) {
        subProcessing(argc, argv, envp);
    } else {
        parentProcessing();
        waitpid(cpid, NULL, 0);
    }
}

void Init() {
    readPort = &Pipe[0];
    writePort = &Pipe[1];

    if (pipe(Pipe) == -1) {
        perror("pipe failed");
        exit(EXIT_FAILURE);
    }

    devNull = open("/dev/null", O_WRONLY);
    Assert(devNull != -1, "open /dev/null failed");

    char *pathVar = getenv("PATH");
    Assert(pathVar != NULL, "PATH environment variable not found");
    strncpy(PATH, pathVar, sizeof(PATH) - 1);
}

int main(int argc, char *argv[], char *envp[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    Init();
    forkRead(argc, argv, envp);
    close(devNull);
    return 0;
}