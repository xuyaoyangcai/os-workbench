#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>

struct data {
    char name[100];
    int pid;
    int ppid;
    int layer;
    char pidstring[100];
};

struct Node {
    struct data item;
    struct Node* parent;
    struct Node* child;
    struct Node* brother;
    int layer;
};

void print(struct Node* node, bool show_pid) {
    if (!node) return;

    for (int i = 0; i < node->layer; i++)
        printf("\t");

    if (show_pid) {
        printf("%s(%s)\n", node->item.name, node->item.pidstring);
    } else {
        printf("%s\n", node->item.name);
    }

    print(node->child, show_pid);
    print(node->brother, show_pid);
}

int compute_layer(struct data* a, struct data* list, int size) {
    if (a->layer >= 0) return a->layer;
    if (a->ppid == 0) return (a->layer = 0);

    for (int i = 0; i < size; i++) {
        if (list[i].pid == a->ppid) {
            a->layer = compute_layer(&list[i], list, size) + 1;
            return a->layer;
        }
    }
    return (a->layer = 0);
}

int compare_alpha(const void *a, const void *b) {
    struct data *d1 = (struct data*)a;
    struct data *d2 = (struct data*)b;
    if (d1->layer != d2->layer) return d1->layer - d2->layer;
    return strcmp(d1->name, d2->name);
}

int compare_numeric(const void *a, const void *b) {
    struct data *d1 = (struct data*)a;
    struct data *d2 = (struct data*)b;
    if (d1->layer != d2->layer) return d1->layer - d2->layer;
    return d1->pid - d2->pid;
}

int filter(const struct dirent* dir) {
    return dir->d_name[0] >= '0' && dir->d_name[0] <= '9';
}

int main(int argc, char *argv[]) {
    bool show_pids = false, numeric_sort = false, show_version = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) show_pids = true;
        if (strcmp(argv[i], "-n") == 0) numeric_sort = true;
        if (strcmp(argv[i], "-V") == 0) show_version = true;
    }

    if (show_version) {
        printf("pstree (PSmisc) 23.1\n");
        return 0;
    }

    struct dirent **namelist;
    int n = scandir("/proc", &namelist, filter, alphasort);
    struct data list[n];

    for (int i = 0; i < n; i++) {
        char path[100], line[100];
        sprintf(path, "/proc/%s/status", namelist[i]->d_name);
        FILE* fp = fopen(path, "r");
        if (!fp) continue;

        fgets(line, 100, fp);
        strcpy(list[i].name, line + 6);
        list[i].name[strcspn(list[i].name, "\n")] = 0;

        while (fgets(line, 100, fp) && strncmp(line, "Pid:", 4) != 0);
        sscanf(line + 5, "%d", &list[i].pid);
        sprintf(list[i].pidstring, "%d", list[i].pid);

        while (fgets(line, 100, fp) && strncmp(line, "PPid:", 5) != 0);
        sscanf(line + 6, "%d", &list[i].ppid);

        list[i].layer = -1;
        fclose(fp);
    }

    for (int i = 0; i < n; i++)
        compute_layer(&list[i], list, n);

    qsort(list, n, sizeof(struct data), numeric_sort ? compare_numeric : compare_alpha);

    struct Node nodes[n];
    memset(nodes, 0, sizeof(nodes));
    for (int i = 0; i < n; i++) {
        nodes[i].item = list[i];
        nodes[i].layer = list[i].layer;
    }

    for (int i = 0; i < n; i++) {
        if (nodes[i].item.ppid == 0) continue;
        for (int j = 0; j < n; j++) {
            if (nodes[j].item.pid == nodes[i].item.ppid) {
                nodes[i].parent = &nodes[j];
                if (!nodes[j].child) {
                    nodes[j].child = &nodes[i];
                } else {
                    struct Node* temp = nodes[j].child;
                    while (temp->brother) temp = temp->brother;
                    temp->brother = &nodes[i];
                }
                break;
            }
        }
    }

    for (int i = 0; i < n; i++) {
        if (nodes[i].item.pid == 1) {
            print(&nodes[i], show_pids);
            break;
        }
    }
    return 0;
}
