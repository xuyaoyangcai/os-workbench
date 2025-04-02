#include "co.h"
#include <stdlib.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#define CO_SIZE 2000

static inline void stack_switch_call(void *sp, void *entry, uintptr_t arg)
{
    asm volatile(
#if __x86_64__
            "movq %%rsp,-0x10(%0); leaq -0x20(%0), %%rsp; movq %2, %%rdi ; call *%1; movq -0x10(%0) ,%%rsp;"
            :
            : "b"((uintptr_t)sp), "d"(entry), "a"(arg)
            : "memory"
#else
        "movl %%esp, -0x8(%0); leal -0xC(%0), %%esp; movl %2, -0xC(%0); call *%1;movl -0x8(%0), %%esp"
		:
		: "b"((uintptr_t)sp), "d"(entry), "a"(arg)
		: "memory"
#endif
            );
}
enum co_status
{
    CO_NEW = 1,
    CO_RUNNING = 2,
    CO_WAITING = 3,
    CO_DEAD = 4,
};

#define STACK_SIZE 4 * 1024 * 8
#define CANARY_SZ 2
#define MAGIC 0x55
struct co
{
    struct co *next;
    void (*func)(void *);
    void *arg;
    char name[50];
    enum co_status status;
    struct co *waiter;
    jmp_buf context;
    uint8_t stack[STACK_SIZE + 1];
};

struct co *current;
struct co *co_start(const char *name, void (*func)(void *), void *arg)
{
    struct co *start = (struct co *)malloc(sizeof(struct co));
    start->arg = arg;
    start->func = func;
    start->status = CO_NEW;
    strcpy(start->name, name);
    if (current == NULL)
    {
        current = (struct co *)malloc(sizeof(struct co));
        current->status = CO_RUNNING; // BUG !! 写成了 current->status==CO_RUNNING;
        current->waiter = NULL;
        strcpy(current->name, "main");
        current->next = current;
    }

    struct co *h = current;
    while (h)
    {
        if (h->next == current)
            break;

        h = h->next;
    }
    assert(h);
    h->next = start;
    start->next = current;
    return start;
}
int times = 1;
void co_wait(struct co *co)
{
    current->status = CO_WAITING;
    co->waiter = current;
    while (co->status != CO_DEAD)
    {
        co_yield ();
    }
    current->status = CO_RUNNING;
    struct co *h = current;

    while (h)
    {
        if (h->next == co)
            break;
        h = h->next;
    }

    h->next = h->next->next;
    free(co);
}
void co_yield ()
{
    if (current == NULL)
    {
        current = (struct co *)malloc(sizeof(struct co));
        current->status = CO_RUNNING;
        strcpy(current->name, "main");
        current->next = current;
    }
    assert(current);
    int val = setjmp(current->context);
    if (val == 0)
    {

        struct co *co_next = current;
        do
        {
            co_next = co_next->next;
        } while (co_next->status == CO_DEAD || co_next->status == CO_WAITING);
        current = co_next;
        if (co_next->status == CO_NEW)
        {
            assert(co_next->status == CO_NEW);
            ((struct co volatile *)current)->status = CO_RUNNING;
            stack_switch_call(&current->stack[STACK_SIZE], current->func, (uintptr_t)current->arg);
            ((struct co volatile *)current)->status = CO_DEAD;
            if (current->waiter)
                current = current->waiter;
        }
        else
        {
            longjmp(current->context, 1);
        }
    }
    else
    {
        return;
    }
}