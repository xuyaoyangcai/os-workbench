#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <assert.h>
#include <ctype.h>

#define MAX_PROCESSES 10000
#define MAX_NAME_LEN 40
#define MAX_CHILDREN 500

typedef struct pidinfo {
    char name[MAX_NAME_LEN];
    pid_t pid;
    pid_t ppid;
} PidInfo;

PidInfo pidinfos[MAX_PROCESSES];
int pid_count = 0;


char* readcmdops(int argc, char* argv[]) {
    if (argc < 2) return NULL;

    char* ops = malloc(strlen(argv[1]) + 1);
    if (!ops) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    // 过滤掉'-'字符
    int j = 0;
    for (int i = 0; argv[1][i]; i++) {
        if (argv[1][i] != '-') {
            ops[j++] = argv[1][i];
        }
    }
    ops[j] = '\0';
    return ops;
}


pid_t readprocessname_ppid(pid_t pid, char name[]) {
    char processpath[256];
    snprintf(processpath, sizeof(processpath), "/proc/%d/stat", pid);

    FILE* fp = fopen(processpath, "r");
    if (!fp) {
        fprintf(stderr, "Failed to open %s\n", processpath);
        return -1;
    }

    pid_t _pid, ppid;
    char pname[MAX_NAME_LEN];
    char state;

    if (fscanf(fp, "%d (%[^)] %c %d", &_pid, pname, &state, &ppid) != 4) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    strncpy(name, pname, MAX_NAME_LEN - 1);
    name[MAX_NAME_LEN - 1] = '\0';

    return ppid;
}


void setProcessInfo() {
    DIR* dp = opendir("/proc");
    if (!dp) {
        perror("opendir");
        exit(EXIT_FAILURE);
    }

    struct dirent* entry;
    while ((entry = readdir(dp)) != NULL) {
        // 跳过非数字目录
        if (!isdigit(entry->d_name[0])) continue;

        pid_t pid = atoi(entry->d_name);
        char name[MAX_NAME_LEN];
        pid_t ppid = readprocessname_ppid(pid, name);

        if (ppid == -1) continue;

        // 防止数组越界
        if (pid_count >= MAX_PROCESSES) {
            fprintf(stderr, "Maximum process limit reached\n");
            break;
        }

        pidinfos[pid_count].pid = pid;
        pidinfos[pid_count].ppid = ppid;
        strncpy(pidinfos[pid_count].name, name, MAX_NAME_LEN - 1);
        pid_count++;
    }
    closedir(dp);
}


void findallchildrens(pid_t pid, int** indices, int* count) {
    *indices = NULL;
    *count = 0;

    for (int i = 0; i < pid_count; i++) {
        if (pidinfos[i].ppid == pid) {
            (*indices) = realloc(*indices, (*count + 1) * sizeof(int));
            (*indices)[*count] = i;
            (*count)++;
        }
    }
}


typedef struct processtree {
    pid_t pid;
    char name[MAX_NAME_LEN];
    struct processtree* children;
    struct processtree* sibling;
} Processtree;


void print_tree(Processtree* node, int depth, int show_pid) {
    if (!node) return;


    printf("%*s", depth * 2, "");
    if (show_pid) {
        printf("%s(%d)-", node->name, node->pid);
    } else {
        printf("%s-", node->name);
    }


    Processtree* child = node->children;
    while (child) {
        printf("\n");
        print_tree(child, depth + 1, show_pid);
        child = child->sibling;
    }
}


Processtree* build_tree(int start_index, int show_pid) {
    if (start_index >= pid_count) return NULL;

    Processtree* root = malloc(sizeof(Processtree));
    root->pid = pidinfos[start_index].pid;
    strncpy(root->name, pidinfos[start_index].name, MAX_NAME_LEN - 1);
    root->children = NULL;
    root->sibling = NULL;

    int* children_indices = NULL;
    int children_count = 0;
    findallchildrens(root->pid, &children_indices, &children_count);

    if (children_count > 0) {
        Processtree* prev = NULL;
        for (int i = 0; i < children_count; i++) {
            Processtree* child = build_tree(children_indices[i], show_pid);
            if (!root->children) {
                root->children = child;
            } else {
                prev->sibling = child;
            }
            prev = child;
        }
        free(children_indices);
    }

    return root;
}

// 改进点8：更清晰的选项处理
int main(int argc, char* argv[]) {
    char* ops = readcmdops(argc, argv);
    int show_pid = 0;

    if (ops) {
        if (strcmp(ops, "p") == 0) {
            show_pid = 1;
        } else if (strcmp(ops, "n") == 0) {
            show_pid = 0;
        } else if (strcmp(ops, "V") == 0) {
            printf("pstree (PSmisc) 23.1\n");
            printf("Copyright (C) 1993-2022 Werner Almesberger and Craig Small\n");
            printf("This is free software; see the source for copying conditions.\n");
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "Invalid option: %s\n", ops);
            free(ops);
            return EXIT_FAILURE;
        }
        free(ops);
    }

    setProcessInfo();

    if (pid_count == 0) {
        printf("No processes found\n");
        return EXIT_SUCCESS;
    }

    Processtree* root = build_tree(0, show_pid);
    print_tree(root, 0, show_pid);
    printf("\n");



    return EXIT_SUCCESS;
}
