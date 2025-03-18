#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>

#define MAX_PROCESSES 10000
#define MAX_NAME_LENGTH 50
#define MAX_CHILDREN 500

typedef struct {
    char name[MAX_NAME_LENGTH];
    pid_t pid;
    pid_t ppid;
} PidInfo;

PidInfo pidinfos[MAX_PROCESSES];
int pid_count = 0;

void read_process_info() {
    DIR *dp = opendir("/proc");
    if (!dp) {
        perror("Cannot open /proc");
        exit(EXIT_FAILURE);
    }
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        int pid = atoi(entry->d_name);
        if (pid == 0) continue;
        char path[40];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE *fp = fopen(path, "r");
        if (!fp) continue;
        fscanf(fp, "%d (%49[^)]) %*c %d", &pidinfos[pid_count].pid, pidinfos[pid_count].name, &pidinfos[pid_count].ppid);
        fclose(fp);
        pid_count++;
    }
    closedir(dp);
}

void find_children(pid_t pid, int index[MAX_CHILDREN]) {
    int count = 0;
    for (int i = 0; i < pid_count; i++) {
        if (pidinfos[i].ppid == pid) {
            index[count++] = i;
        }
    }
    index[count] = -1;
}

void print_tree(pid_t pid, int indent, int show_pid) {
    for (int i = 0; i < indent; i++) printf("  ");
    for (int i = 0; i < pid_count; i++) {
        if (pidinfos[i].pid == pid) {
            printf("%s%s", pidinfos[i].name, show_pid ? " (%d)" : "", pid);
            break;
        }
    }
    printf("\n");
    int children[MAX_CHILDREN];
    find_children(pid, children);
    for (int i = 0; children[i] != -1; i++) {
        print_tree(pidinfos[children[i]].pid, indent + 1, show_pid);
    }
}

void print_version() {
    printf("pstree (PSmisc) UNKNOWN\n");
    printf("Copyright (C) 1993-2019 Werner Almesberger and Craig Small\n");
    printf("PSmisc comes with ABSOLUTELY NO WARRANTY.\n");
    printf("This is free software, and you are welcome to redistribute it under\n");
    printf("the terms of the GNU General Public License.\n");
}

int main(int argc, char *argv[]) {
    read_process_info();
    int show_pid = 0;
    if (argc > 1) {
        if (strcmp(argv[1], "-p") == 0) {
            show_pid = 1;
        } else if (strcmp(argv[1], "-V") == 0) {
            print_version();
            return 0;
        } else {
            fprintf(stderr, "Invalid option\n");
            return EXIT_FAILURE;
        }
    }
    print_tree(1, 0, show_pid); // 假设 1 号进程是根进程
    return 0;
}
