#include "co.h"
#include <stdlib.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#define STACK_SIZE 4 * 1024 * 8 // 32 KiB stack per coroutine

static inline void stack_switch_call(void *sp, void *entry, uintptr_t arg) {
    asm volatile(
#if __x86_64__
            "movq %%rsp,-0x10(%0); leaq -0x20(%0), %%rsp; movq %2, %%rdi ; call *%1; movq -0x10(%0) ,%%rsp;"
            :
            : "b"((uintptr_t)sp), "d"(entry), "a"(arg)
            : "memory"
#else
        "movl %%esp, -0x8(%0); leal -0xC(%0), %%esp; movl %2, -0xC(%0); call *%1; movl -0x8(%0), %%esp"
        :
        : "b"((uintptr_t)sp), "d"(entry), "a"(arg)
        : "memory"
#endif
            );
}

enum co_status {
    CO_NEW = 1,       // 新创建，还未执行过
    CO_RUNNING = 2,   // 已经执行过
    CO_WAITING = 3,   // 在 co_wait 上等待
    CO_DEAD = 4,      // 已经结束，但还未释放资源
};

struct co {
    struct co *next;
    void (*func)(void *);
    void *arg;
    char name[50];
    enum co_status status; // 协程的状态
    struct co *waiter;     // 是否有其他协程在等待当前协程
    jmp_buf context;       // 寄存器现场 (setjmp.h)
    uint8_t stack[STACK_SIZE]; // 协程的堆栈
};

struct co *current = NULL;

struct co *co_start(const char *name, void (*func)(void *), void *arg) {
    struct co *start = (struct co *)malloc(sizeof(struct co));
    start->arg = arg;
    start->func = func;
    start->status = CO_NEW;
    strcpy(start->name, name);
    if (current == NULL) { // 初始化 main 协程
        current = (struct co *)malloc(sizeof(struct co));
        current->status = CO_RUNNING;
        current->waiter = NULL;
        strcpy(current->name, "main");
        current->next = current;
    }
    // 将新协程加入环形链表
    struct co *h = current;
    while (h->next != current) {
        h = h->next;
    }
    h->next = start;
    start->next = current;
    return start;
}

void co_wait(struct co *co) {
    current->status = CO_WAITING;
    co->waiter = current;
    while (co->status != CO_DEAD) {
        co_yield();
    }
    current->status = CO_RUNNING;
    struct co *h = current;
    while (h->next != co) {
        h = h->next;
    }
    // 从环形链表中删除 co
    h->next = h->next->next;
    free(co);
}

void co_yield() {
    assert(current);
    int val = setjmp(current->context);
    if (val == 0) {
        struct co *co_next = current;
        do {
            co_next = co_next->next;
        } while (co_next->status == CO_DEAD || co_next->status == CO_WAITING);
        current = co_next;
        if (co_next->status == CO_NEW) {
            co_next->status = CO_RUNNING;
            stack_switch_call(&co_next->stack[STACK_SIZE], co_next->func, (uintptr_t)co_next->arg);
            co_next->status = CO_DEAD;
            if (co_next->waiter) {
                current = co_next->waiter;
            }
        } else {
            longjmp(co_next->context, 1);
        }
    }
}