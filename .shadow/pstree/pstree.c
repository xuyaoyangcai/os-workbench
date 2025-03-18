#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_PROCESSES 10000
#define MAX_NAME_LEN 50

typedef struct pidinfo {
    char name[MAX_NAME_LEN];
    pid_t pid;
    pid_t ppid;
} PidInfo;

PidInfo pidinfos[MAX_PROCESSES];
int pid_count = 0;

char* get_option(int argc, char *argv[]) {
    if (argc < 2) return NULL;
    return argv[1] + 1;
}

pid_t get_process_info(pid_t pid, char name[]) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    pid_t _pid, ppid;
    char pname[MAX_NAME_LEN];
    char status;
    fscanf(fp, "%d (%49[^)]) %c %d", &_pid, pname, &status, &ppid);

    fclose(fp);
    strncpy(name, pname, MAX_NAME_LEN);
    return ppid;
}

void load_process_info() {
    DIR *dp = opendir("/proc");
    if (!dp) {
        perror("Failed to open /proc");
        exit(EXIT_FAILURE);
    }

    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        int pid = atoi(entry->d_name);
        if (pid > 0) {
            pidinfos[pid_count].pid = pid;
            pidinfos[pid_count].ppid = get_process_info(pid, pidinfos[pid_count].name);
            pid_count++;
        }
    }
    closedir(dp);
}

typedef struct ProcessTree {
    pid_t pid;
    char name[MAX_NAME_LEN];
    struct ProcessTree *children[MAX_PROCESSES];
    int child_count;
} ProcessTree;

void find_children(pid_t pid, int indexes[], int *count) {
    *count = 0;
    for (int i = 0; i < pid_count; i++) {
        if (pidinfos[i].ppid == pid) {
            indexes[(*count)++] = i;
        }
    }
}

void build_tree(ProcessTree *node) {
    int indexes[MAX_PROCESSES], count;
    find_children(node->pid, indexes, &count);
    node->child_count = count;

    for (int i = 0; i < count; i++) {
        node->children[i] = (ProcessTree *)malloc(sizeof(ProcessTree));
        node->children[i]->pid = pidinfos[indexes[i]].pid;
        strcpy(node->children[i]->name, pidinfos[indexes[i]].name);
        node->children[i]->child_count = 0;
        build_tree(node->children[i]);
    }
}

void print_tree(ProcessTree *node, int depth, int show_pid) {
    for (int i = 0; i < depth; i++) {
        printf("  ");
    }
    printf("%s", node->name);
    if (show_pid) {
        printf("(%d)", node->pid);
    }
    printf("\n");

    for (int i = 0; i < node->child_count; i++) {
        print_tree(node->children[i], depth + 1, show_pid);
    }
}

void free_tree(ProcessTree *node) {
    for (int i = 0; i < node->child_count; i++) {
        free_tree(node->children[i]);
        free(node->children[i]);
    }
}

int main(int argc, char *argv[]) {
    char *option = get_option(argc, argv);
    load_process_info();

    ProcessTree *root = (ProcessTree *)malloc(sizeof(ProcessTree));
    root->pid = pidinfos[0].pid;
    strcpy(root->name, pidinfos[0].name);
    root->child_count = 0;

    build_tree(root);

    if (!option) {
        print_tree(root, 0, 0);
    } else if (strcmp(option, "p") == 0) {
        print_tree(root, 0, 1);
    } else if (strcmp(option, "n") == 0) {
        print_tree(root, 0, 0);
    } else if (strcmp(option, "V") == 0) {
        printf("pstree (PSmisc) UNKNOWN\n");
        printf("Copyright (C) 1993-2019 Werner Almesberger and Craig Small\n");
        printf("PSmisc comes with ABSOLUTELY NO WARRANTY.\n");
        printf("This is free software, and you are welcome to redistribute it under\n");
        printf("the terms of the GNU General Public License.\n");
    } else {
        fprintf(stderr, "Invalid option\n");
        return EXIT_FAILURE;
    }

    free_tree(root);
    free(root);
    return EXIT_SUCCESS;
}
