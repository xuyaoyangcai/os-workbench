#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <limits.h>
#include <dirent.h>
#include <stdbool.h>

#define MAXPID_T 100000

struct proc_node {
    pid_t pid;
    pid_t ppid;
    char filename[20];
};

void printPsTree(struct proc_node *procs, pid_t pid, int level, int ops);
int listProc(struct proc_node *procs);
bool isNum(char *s);

int main(int argc, char *argv[]) {
    int ops = 0;
    static struct option long_options[] = {
            {"show-pids",       no_argument,    NULL,  'p'},
            {"numeric-sort",    no_argument,    NULL,  'n'},
            {"version",         no_argument,    NULL,  'V'},
            {NULL,              0,              NULL,   0}
    };
    char opt;
    struct proc_node procs[MAXPID_T];
    for (int i = 0; i < argc; i++) {
        assert(argv[i]);
        printf("argv[%d] = %s\n", i, argv[i]);
    }
    assert(!argv[argc]);
    while(-1 != (opt = getopt_long(argc, argv, "pnV", long_options, NULL))){
        switch(opt) {
            case 'p':
                ops = ops | 0x1;
                break;
            case 'n':
                ops = ops | 0x2;
                break;
            case 'V':
                ops = ops | 0x4;
                break;
            default:
                printf("wrong argument\n");
                break;
        }
    }
    if (listProc(procs) == 0) {
        switch (ops) {
            case 0:
                printf("%s\n", procs[1].filename);
                printPsTree(procs, 1, 1, ops);
                break;
            case 2:
                printf("%s\n", procs[1].filename);
                printPsTree(procs, 1, 1, ops);
                break;
            case 4:
            case 6:
                printf("pstree (PSmisc) UNKNOWN\nCopyright (C) 1993-2019 Werner Almesberger and Craig Small\n\nPSmisc comes with ABSOLUTELY NO WARRANTY.\nThis is free software, and you are welcome to redistribute it under\nthe terms of the GNU General Public License.\nFor more information about these matters, see the files named COPYING.\n");
                if (ops == 6) {
                    printf("%s\n", procs[1].filename);
                    printPsTree(procs, 1, 1, ops);
                }
                break;
            case 1:
            case 3:
            case 5:
            case 7:
                if (ops >= 5) {
                    printf("pstree (PSmisc) UNKNOWN\nCopyright (C) 1993-2019 Werner Almesberger and Craig Small\n\nPSmisc comes with ABSOLUTELY NO WARRANTY.\nThis is free software, and you are welcome to redistribute it under\nthe terms of the GNU General Public License.\nFor more information about these matters, see the files named COPYING.\n");
                }
                printf("%s(%d)\n", procs[1].filename, 1);
                printPsTree(procs, 1, 1, ops);
                break;
            default:
                break;
        }
    } else {
        exit(1);
    }
    return 0;
}

void printPsTree(struct proc_node *procs, pid_t pid, int level, int ops) {
    for (int i = pid; i < MAXPID_T; i++) {
        if (procs[i].ppid == pid) {
            for (int j = 0; j < level; j++) {
                printf("\t");
            }
            if (ops & 0x1) {
                printf("%s(%d)\n", procs[i].filename, i);
            } else {
                printf("%s\n", procs[i].filename);
            }
            printPsTree(procs, i, level + 1, ops);
        }
    }
    return;
}

int listProc(struct proc_node *procs) {
    DIR *d;
    struct dirent *dir;
    d = opendir("/proc/");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (isNum(dir->d_name)) {
                char filename[300];
                int pid, ppid;
                char exe[256];
                snprintf(filename, sizeof(filename), "/proc/%s/stat", dir->d_name);
                FILE *fp = fopen(filename, "r");
                if (fp) {
                    fscanf(fp, "%d %s %*c %d", &pid, exe, &ppid);
                    exe[0] = '{';
                    exe[strlen(exe)-1] = '}';
                    procs[pid].pid = pid;
                    procs[pid].ppid = ppid;
                    strcpy(procs[pid].filename, exe);
                    fclose(fp);
                } else {
                    return 1;
                }
            }
        }
        closedir(d);
    }
    return 0;
}

bool isNum(char *s) {
    if (s == NULL) {
        return false;
    }
    for (int i = 0; i < strlen(s); i++) {
        if (!(s[i] >= '0' && s[i] <= '9')) {
            return false;
        }
    }
    return true;
}
