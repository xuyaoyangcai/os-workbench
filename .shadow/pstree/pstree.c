#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#define MAX_PROCS 32768  // 进程数量最大限制

typedef struct Process {
    int pid;
    int ppid;
    char name[256];
} Process;

typedef struct Node {
    Process proc;
    struct Node* children[MAX_PROCS];
    int child_count;
} Node;

Process processes[MAX_PROCS];
int proc_count = 0;
int show_pids = 0, numeric_sort = 0, version_flag = 0;

// 读取 /proc 目录获取所有进程信息
void read_proc_info() {
    DIR* dir = opendir("/proc");
    if (!dir) {
        perror("opendir");
        exit(EXIT_FAILURE);
    }

    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (!isdigit(entry->d_name[0])) continue;  // 只处理数字 PID

        char path[256], line[256];
        snprintf(path, sizeof(path), "/proc/%s/status", entry->d_name);
        FILE* file = fopen(path, "r");
        if (!file) continue;

        Process proc = {0};
        proc.pid = atoi(entry->d_name);

        while (fgets(line, sizeof(line), file)) {
            if (strncmp(line, "Name:", 5) == 0) {
                sscanf(line, "Name: %255s", proc.name);
            } else if (strncmp(line, "PPid:", 5) == 0) {
                sscanf(line, "PPid: %d", &proc.ppid);
            }
        }
        fclose(file);

        processes[proc_count++] = proc;
    }
    closedir(dir);
}

// 递归打印进程树
void print_tree(int ppid, int depth) {
    for (int i = 0; i < proc_count; i++) {
        if (processes[i].ppid == ppid) {
            for (int j = 0; j < depth; j++) {
                printf("  ");  // 缩进
            }
            printf("%s", processes[i].name);
            if (show_pids) {
                printf("(%d)", processes[i].pid);
            }
            printf("\n");
            print_tree(processes[i].pid, depth + 1);
        }
    }
}

int main(int argc, char* argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "pnV")) != -1) {
        switch (opt) {
            case 'p':
                show_pids = 1;
                break;
            case 'n':
                numeric_sort = 1;
                break;
            case 'V':
                version_flag = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [-p] [-n] [-V]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (version_flag) {
        printf("pstree version 1.0\n");
        exit(EXIT_SUCCESS);
    }

    read_proc_info();

    // 默认从 PID=1 开始打印进程树
    print_tree(1, 0);
    return 0;
}
