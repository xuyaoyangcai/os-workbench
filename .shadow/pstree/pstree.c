#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_PID_LENGTH 10

// 定义进程信息结构体
typedef struct Process {
    pid_t pid;
    pid_t ppid;
    char name[256];
    struct Process* next;
    struct Process* children;
} Process;

// 函数声明
Process* create_process(pid_t pid, pid_t ppid, const char* name);
void add_child(Process* parent, Process* child);
void free_process_tree(Process* root);
void print_process_tree(Process* root, int indent_level, int show_pids);
Process* get_process_info(pid_t pid);
int parse_pid_file(const char* file_path, pid_t* pid);
int parse_name_file(const char* file_path, char* name);

// 获取进程信息
Process* get_process_info(pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    pid_t ppid;
    char name[256];
    if (parse_pid_file(path, &ppid) == -1 || parse_name_file(path, name) == -1) {
        return NULL;
    }

    return create_process(pid, ppid, name);
}

// 创建一个进程节点
Process* create_process(pid_t pid, pid_t ppid, const char* name) {
    Process* new_process = (Process*)malloc(sizeof(Process));
    if (!new_process) {
        perror("Failed to allocate memory for process");
        return NULL;
    }
    new_process->pid = pid;
    new_process->ppid = ppid;
    strncpy(new_process->name, name, sizeof(new_process->name) - 1);
    new_process->name[sizeof(new_process->name) - 1] = '\0';
    new_process->next = NULL;
    new_process->children = NULL;
    return new_process;
}

// 添加子进程
void add_child(Process* parent, Process* child) {
    if (parent->children == NULL) {
        parent->children = child;
    } else {
        Process* temp = parent->children;
        while (temp->next != NULL) {
            temp = temp->next;
        }
        temp->next = child;
    }
}

// 解析 `/proc/[pid]/stat` 文件，获取父进程 ID
int parse_pid_file(const char* file_path, pid_t* ppid) {
    FILE* fp = fopen(file_path, "r");
    if (!fp) {
        return -1;
    }

    int ret = fscanf(fp, "%*d %*s %*c %d", ppid);
    fclose(fp);
    return ret == 1 ? 0 : -1;
}

// 解析 `/proc/[pid]/stat` 文件，获取进程名称
int parse_name_file(const char* file_path, char* name) {
    FILE* fp = fopen(file_path, "r");
    if (!fp) {
        return -1;
    }

    int ret = fscanf(fp, "%*d %255s", name);
    fclose(fp);
    return ret == 1 ? 0 : -1;
}

// 打印进程树
void print_process_tree(Process* root, int indent_level, int show_pids) {
    if (!root) return;

    // 打印缩进
    for (int i = 0; i < indent_level; i++) {
        printf("  ");
    }

    printf("%s", root->name);
    if (show_pids) {
        printf(" (%d)", root->pid);
    }
    printf("\n");

    // 打印子进程
    Process* child = root->children;
    while (child != NULL) {
        print_process_tree(child, indent_level + 1, show_pids);
        child = child->next;
    }
}

// 递归释放进程树
void free_process_tree(Process* root) {
    if (!root) return;

    Process* child = root->children;
    while (child != NULL) {
        Process* next_child = child->next;
        free_process_tree(child);
        child = next_child;
    }

    free(root);
}

// 获取所有的进程 PID 列表
void get_all_processes(Process** processes) {
    DIR* dir = opendir("/proc");
    if (!dir) {
        perror("Failed to open /proc directory");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (isdigit(entry->d_name[0])) {
            pid_t pid = atoi(entry->d_name);
            Process* proc = get_process_info(pid);
            if (proc) {
                processes[pid] = proc;
            }
        }
    }

    closedir(dir);
}

// 构建进程树
void build_process_tree(Process** processes) {
    for (int i = 0; i < 32768; i++) {
        if (processes[i] != NULL) {
            pid_t ppid = processes[i]->ppid;
            if (ppid != 0 && ppid != i) {
                if (processes[ppid] != NULL) {
                    add_child(processes[ppid], processes[i]);
                }
            }
        }
    }
}

// 解析命令行参数
void parse_args(int argc, char* argv[], int* show_pids, int* numeric_sort) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--show-pids") == 0) {
            *show_pids = 1;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--numeric-sort") == 0) {
            *numeric_sort = 1;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("pstree version 1.0\n");
            exit(0);
        }
    }
}

int main(int argc, char* argv[]) {
    int show_pids = 0;
    int numeric_sort = 0;
    parse_args(argc, argv, &show_pids, &numeric_sort);

    Process* processes[32768] = {0};
    get_all_processes(processes);
    build_process_tree(processes);

    // 假设进程 ID 1 是 root 进程
    if (processes[1]) {
        print_process_tree(processes[1], 0, show_pids);
    }

    // 释放内存
    for (int i = 0; i < 32768; i++) {
        free_process_tree(processes[i]);
    }

    return 0;
}

