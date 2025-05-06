// parallel.h (新文件，工具函数)
#include "thread.h"

// 并行for b in [0, B)
static inline void parallel_for(int B, void (*worker)(int b, void *arg), void *arg) {
    struct task_args {
        int B;
        void (*worker)(int, void *);
        void *arg;
    };
    struct task_args *args = malloc(sizeof(struct task_args));
    args->B = B;
    args->worker = worker;
    args->arg = arg;

    void thread_entry(int tid) {
        int nthreads = n_; // 线程总数
        for (int b = tid - 1; b < args->B; b += nthreads) {
            args->worker(b, args->arg);
        }
    }

    for (int i = 0; i < n_; i++) {
        create(thread_entry);
    }
    join();
    free(args);
}
