#ifndef CPU_BINDING_H
#define CPU_BINDING_H

#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// AMD Ryzen 7 8845HS 超线程配对
// Core 0: CPU 0, 8
// Core 1: CPU 1, 9
// Core 2: CPU 2, 10
// Core 3: CPU 3, 11
// Core 4: CPU 4, 12
// Core 5: CPU 5, 13
// Core 6: CPU 6, 14
// Core 7: CPU 7, 15

// 同一核心的超线程对（用于测试超线程共享缓存）
static const int HT_PAIRS[][2] = {
    {0, 8}, {1, 9}, {2, 10}, {3, 11},
    {4, 12}, {5, 13}, {6, 14}, {7, 15}
};

// 不同核心的 CPU（用于测试独立缓存）
static const int DIFFERENT_CORES[] = {0, 1, 2, 3, 4, 5, 6, 7};

// 绑定当前线程到指定 CPU
static inline int bind_to_cpu(int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);

    int ret = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        perror("sched_setaffinity failed");
        return -1;
    }
    return 0;
}

// 绑定指定线程到指定 CPU
static inline int bind_thread_to_cpu(pthread_t thread, int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);

    int ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        fprintf(stderr, "pthread_setaffinity_np failed: %d\n", ret);
        return -1;
    }
    return 0;
}

// 获取当前线程运行的 CPU
static inline int get_current_cpu(void) {
    return sched_getcpu();
}

// 打印 CPU 绑定信息
static inline void print_cpu_bindind(const char *name) {
    printf("[%s] Running on CPU %d\n", name, get_current_cpu());
}

// 绑定到同一核心的两个超线程
static inline void bind_to_same_core_ht(int core_id, int *cpu1, int *cpu2) {
    if (core_id < 0 || core_id > 7) {
        fprintf(stderr, "Invalid core_id: %d (must be 0-7)\n", core_id);
        return;
    }
    *cpu1 = HT_PAIRS[core_id][0];
    *cpu2 = HT_PAIRS[core_id][1];
}

// 绑定到不同核心
static inline void bind_to_different_cores(int *cpu1, int *cpu2) {
    *cpu1 = DIFFERENT_CORES[0];  // Core 0, CPU 0
    *cpu2 = DIFFERENT_CORES[1];  // Core 1, CPU 1
}

// 高精度计时器
#include <time.h>

static inline double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// 内存屏障
#define BARRIER() __asm__ __volatile__("mfence" ::: "memory")
#define COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")

#endif // CPU_BINDING_H
