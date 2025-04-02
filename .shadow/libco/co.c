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
    CO_NEW = 1,
    CO_RUNNING = 2,
    CO_WAITING = 3,
    CO_DEAD = 4,
};

struct co {
    struct co *next;
    void (*func)(void *);
    void *arg;
    char name[50];
    enum co_status status;
    struct co *waiter;
    jmp_buf context;
    uint8_t stack[STACK_SIZE];
};

struct co *current = NULL;
struct co *ready_queue = NULL; // 就绪队列

struct co *co_start(const char *name, void (*func)(void *), void *arg) {
    struct co *start = (struct co *)malloc(sizeof(struct co));
    start->arg = arg;
    start->func = func;
    start->status = CO_NEW;
    strcpy(start->name, name);
    if (current == NULL) {
        current = (struct co *)malloc(sizeof(struct co));
        current->status = CO_RUNNING;
        current->waiter = NULL;
        strcpy(current->name, "main");
        current->next = current;
    }

    struct co *h = current;
    while (h->next != current) {
        h = h->next;
    }
    h->next = start;
    start->next = current;

    // 将新协程加入就绪队列
    start->next = ready_queue;
    ready_queue = start;
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
    h->next = h->next->next;
    free(co);
}

void co_yield() {
    assert(current);
    int val = setjmp(current->context);
    if (val == 0) {
        struct co *co_next = ready_queue;
        if (co_next) {
            ready_queue = co_next->next;
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
        } else {
            // 如果没有其他协程就绪，切换回 main
            current = current->next;
        }
    }
}