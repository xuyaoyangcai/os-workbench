
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>
#include <stdbool.h>

typedef struct Process {
    int processID;
    char processName[256];
    int parentID;
    int parentIndex;  // 父亲在列表中的下标
    struct Process* children[100]; // 假设每个进程最多有100个孩子
    int childrenCnt;
} Process;

bool isNumeric(const char *str) {
    while (*str) {
        if (*str < '0' || *str > '9') return false;
        str++;
    }
    return true;
}

void getProcessList(Process* processList, int* count) {
    assert(count);
    int cnt = 0;
    assert(processList);
    DIR* procDir = opendir("/proc");
    assert(procDir);

    struct dirent* entry;
    while ((entry = readdir(procDir)) != NULL) {
        if (entry->d_type == DT_DIR && isNumeric(entry->d_name)) {
            char statusPath[512];
            char line[256];
            FILE* statusFile;
            snprintf(statusPath, sizeof(statusPath), "/proc/%s/status", entry->d_name);
            statusFile = fopen(statusPath, "r");
            if (!statusFile) continue;

            while (fgets(line, sizeof(line), statusFile) != NULL) {
                if (strncmp(line, "Name:", 5) == 0) {
                    sscanf(line, "Name:\t%s", processList[cnt].processName);
                }
                if (strncmp(line, "Pid:", 4) == 0) {
                    sscanf(line, "Pid:\t%d", &processList[cnt].processID);
                }
                if (strncmp(line, "PPid:", 5) == 0) {
                    sscanf(line, "PPid:\t%d", &processList[cnt].parentID);
                }
            }
            fclose(statusFile);
            processList[cnt].childrenCnt = 0;
            cnt++;
        }
    }
    *count = cnt;
    closedir(procDir);
}

int getParentIndex(Process* processList, int i) {
    int a = 0, b = i - 1;
    while (b >= a + 1) {
        if (processList[b].processID == processList[i].parentID)
            return b;
        int k = (b - a) / 2 + a;
        if (processList[k].processID == processList[i].parentID)
            return k;
        else if (processList[k].processID > processList[i].parentID)
            b = k;
        else
            a = k;
    }
    return -1;
}

void buildProcessTree(Process* processList, int* count) {
    for (int i = 0; i < *count; i++) {
        int index = getParentIndex(processList, i);
        if (index == -1) continue;
        processList[index].children[processList[index].childrenCnt++] = &processList[i];
        assert(processList[index].childrenCnt <= 100);
    }
}

void displayTree(Process* proc, int tabCnt, bool isFirst) {
    if (isFirst)
        printf("|");
    else {
        for (int i = 0; i < tabCnt; i++) {
            printf("\t");
        }
    }
    printf("%s(%d)(%d)\n", proc->processName, proc->processID, proc->parentID);

    for (int i = 0; i < proc->childrenCnt; i++) {
        displayTree(proc->children[i], tabCnt + 2, i == 0);
    }
}

int main(int argc, char *argv[]) {
    Process processList[500];
    int count = 0;
    int opcode = 0;

    for (int i = 0; i < argc; i++) {
        assert(argv[i]);
        if (strncmp(argv[i], "-p", 2) == 0 || strncmp(argv[i], "--show-pids", 11) == 0) {
            opcode = 1;
            break;
        }
        if (strncmp(argv[i], "-n", 2) == 0 || strncmp(argv[i], "--numeric-sort", 14) == 0) {
            opcode = 2;
            break;
        }
        if (strncmp(argv[i], "-V", 2) == 0 || strncmp(argv[i], "--version", 9) == 0) {
            opcode = 3;
            break;
        }
    }
    assert(!argv[argc]);

    switch (opcode) {
        case 1:
            getProcessList(processList, &count);
            buildProcessTree(processList, &count);
            displayTree(&processList[0], 0, 0);
            break;
        case 2:
            break;
        case 3:
            break;
        default:
            printf("Display a tree of processes.\n");
            printf("  -p, --show-pids\tshow PIDs\n");
            printf("  -n, --numeric-sort\tsort output by PID\n");
    }
    printf("There are %d processes\n", count);
    return 0;
}
