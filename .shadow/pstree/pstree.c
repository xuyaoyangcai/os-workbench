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
    char ppidstring[100];
};

struct Node {
    struct data item;
    struct Node* parent;
    struct Node* child;
    struct Node* brother;
    int layer;
};

void print(struct Node* node, bool show_pid) {
    if (node == NULL) {
        return;
    }
    for (int i = 0; i < node->layer; i++) {
        printf("\t");
    }
    if (show_pid) {
        printf("(%d) %s", node->item.pid, node->item.name);
    } else {
        printf("%s", node->item.name);
    }
    printf("\n");
    print(node->child, show_pid);  // 先打印子节点
    print(node->brother, show_pid); // 再打印兄弟节点
}

void swap_data(struct data* pd1, struct data* pd2) {
    struct data tmp;
    struct data* ptmp = &tmp;
    strcpy(ptmp->name, pd1->name);
    strcpy(ptmp->pidstring, pd1->pidstring);
    ptmp->pid = pd1->pid;
    ptmp->ppid = pd1->ppid;
    ptmp->layer = pd1->layer;

    strcpy(pd1->name, pd2->name);
    strcpy(pd1->pidstring, pd2->pidstring);
    pd1->pid = pd2->pid;
    pd1->ppid = pd2->ppid;
    pd1->layer = pd2->layer;

    strcpy(pd2->name, ptmp->name);
    strcpy(pd2->pidstring, ptmp->pidstring);
    pd2->pid = ptmp->pid;
    pd2->ppid = ptmp->ppid;
    pd2->layer = ptmp->layer;
}

void copy_data(struct data* pd1, struct data* pd2) {
    strcpy(pd1->name, pd2->name);
    strcpy(pd1->pidstring, pd2->pidstring);
    pd1->layer = pd2->layer;
    pd1->pid = pd2->pid;
    pd1->ppid = pd2->ppid;
}

int compute_layer(struct data* a, struct data* list) {
    if (a->layer > 0)
        return a->layer;
    if (a->ppid == 0) {
        a->layer = 0;
        return a->layer;
    } else {
        struct data* pdata;
        for (pdata = list; pdata->pid != a->ppid; pdata++);
        assert(pdata->pid == a->ppid);
        a->layer = compute_layer(pdata, list) + 1;
        return a->layer;
    }
}

int my_filter(const struct dirent* dir) {
    if ((dir->d_name)[0] >= '0' && (dir->d_name)[0] <= '9')
        return 1;
    else
        return 0;
}

int main(int argc, char *argv[]) {
    typedef bool pstree_option;
    pstree_option pstree_show_pids, pstree_numeric_sort, pstree_version;
    pstree_show_pids = pstree_numeric_sort = pstree_version = false;

    for (int i = 0; i < argc; i++) {
        assert(argv[i]);
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--show-pids") == 0)
            pstree_show_pids = true;
        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "numeric-sort") == 0)
            pstree_numeric_sort = true;
        if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0)
            pstree_version = true;
    }

    if (pstree_version == true) {
        fprintf(stderr, "pstree (PSmisc) 23.1\nCopyright (C) 1993-2017 Werner Almesberger and Craig Small\n\nPSmisc 不提供任何保证。\n该程序为自由软件，欢迎你在 GNU 通用公共许可证 (GPL) 下重新发布。\n详情可参阅 COPYING 文件。\n");
        return 0;
    }

    struct dirent **namelist;
    int n;
    n = scandir("/proc", &namelist, my_filter, alphasort);
    int nn = n;
    char current_path[100];
    char one_line[100];
    struct data list[nn];

    for (int i = 0; i < nn; i++) {
        strcat(strcat(strcpy(current_path, "/proc/"), namelist[i]->d_name), "/status");
        FILE* fp = fopen(current_path, "r");
        fgets(one_line, 100, fp);
        strcpy(list[i].name, one_line + 6);
        while (strncmp(one_line, "Pid:", 4) != 0) {
            fgets(one_line, 100, fp);
        } // get pid
        strcpy(list[i].pidstring, one_line + 5);
        for (int j = 0; list[i].pidstring[j] != '\0'; j++) {
            if (list[i].pidstring[j] == '\n') {
                list[i].pidstring[j] = '\0';
            }
        }
        list[i].pid = atoi(one_line + 5);
        while (strncmp(one_line, "PPid:", 5) != 0) {
            fgets(one_line, 100, fp);
        } // get ppid
        list[i].ppid = atoi(one_line + 6);
        list[i].layer = -1;
        fclose(fp);
    }

    for (int i = 0; i < nn; i++) {
        list[i].layer = compute_layer(&list[i], list);
    }

    if (pstree_numeric_sort == false) {
        struct data tmp;
        for (int i = 0; i < nn - 1; i++) {
            for (int j = 0; j < nn - 1 - i; j++) {
                if (list[j].layer > list[j + 1].layer) {
                    swap_data(&list[j], &list[j + 1]);
                } else if (list[j].layer == list[j + 1].layer) {
                    if (list[j].ppid > list[j + 1].ppid) {
                        swap_data(&list[j], &list[j + 1]);
                    } else if (list[j].ppid == list[j + 1].ppid) {
                        if (list[j].pid > list[j + 1].pid) {
                            swap_data(&list[j], &list[j + 1]);
                        }
                    }
                }
            }
        }
    }

    struct Node Nodelist[nn];
    memset(&Nodelist, 0, nn * sizeof(struct Node));
    for (int i = 0; i < nn; i++) {
        copy_data(&Nodelist[i].item, &list[i]);
        Nodelist[i].layer = Nodelist[i].item.layer;
        if (Nodelist[i].item.ppid == 0) {
            Nodelist[i].parent = NULL;
            Nodelist[i].brother = NULL;
        }
    }

    for (int i = 0; i < nn; i++) {
        if (Nodelist[i].item.ppid == 0)
            continue;
        else {
            for (int j = 0; j < nn; j++) {
                if (Nodelist[j].item.pid == Nodelist[i].item.ppid) {
                    Nodelist[i].parent = &Nodelist[j];
                    if (Nodelist[j].child == NULL) {
                        Nodelist[j].child = &Nodelist[i];
                    } else {
                        struct Node* ptrNode = Nodelist[j].child;
                        while (ptrNode->brother != NULL) {
                            ptrNode = ptrNode->brother;
                        }
                        ptrNode->brother = &Nodelist[i];
                    }
                    break;
                }
            }
        }
    }

    int start = 0;
    for (int i = 0; i < nn; i++) {
        if (Nodelist[i].item.pid == 1)
            start = i;
    }
    print(&Nodelist[start], pstree_show_pids);

    return 0;
}
