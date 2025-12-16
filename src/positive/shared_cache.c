/*
 * shared_cache.c - 共享缓存协作加速测试
 *
 * 演示同一核心的两个超线程访问相同/相近内存区域时，
 * 由于共享 L1/L2 缓存，可以获得协作加速效果。
 *
 * 编译: gcc -O2 -pthread -o shared_cache shared_cache.c
 * 运行: ./shared_cache [--same-core | --diff-core | --single | --all]
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include "../common/cpu_bindind.h"
#include "../common/prefetch_utils.h"

// 配置参数
// 数组大小小于 L1 cache，确保数据能被缓存
#define ARRAY_SIZE (16 * 1024)  // 16KB < 32KB L1 cache
#define ELEMENTS (ARRAY_SIZE / sizeof(uint64_t))
#define ITERATIONS 100000

// 共享数据数组
static uint64_t shared_array[ELEMENTS] CACHE_ALIGNED;

// 线程参数
typedef struct {
    int cpu_id;
    int thread_id;
    int start_idx;
    int end_idx;
    volatile int *ready;
    volatile int *start;
    uint64_t result;
    double elapsed_time;
} thread_arg_t;

// 顺序访问 - 良好的空间局部性
static uint64_t sequential_access(int start, int end) {
    uint64_t sum = 0;

    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (int i = start; i < end; i++) {
            sum += shared_array[i];
            shared_array[i] = sum & 0xFF;
        }
    }

    return sum;
}

// 线程工作函数
static void *worker_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;

    bind_to_cpu(targ->cpu_id);
    print_cpu_bindind(targ->thread_id == 0 ? "Thread0" : "Thread1");

    __atomic_fetch_add(targ->ready, 1, __ATOMIC_SEQ_CST);
    while (*targ->start == 0) {
        __asm__ __volatile__("pause" ::: "memory");
    }

    double start = get_time_sec();
    targ->result = sequential_access(targ->start_idx, targ->end_idx);
    targ->elapsed_time = get_time_sec() - start;

    return NULL;
}

// 单线程测试 - 处理整个数组
static void run_single_thread(void) {
    printf("\n=== Single Thread Test ===\n");

    bind_to_cpu(0);
    print_cpu_bindind("SingleThread");

    double start = get_time_sec();
    uint64_t result = sequential_access(0, ELEMENTS);
    double elapsed = get_time_sec() - start;

    printf("Result: %lu\n", result);
    printf("Time: %.4f seconds\n", elapsed);
    printf("Throughput: %.2f M ops/sec\n",
           (double)(ELEMENTS * ITERATIONS) / elapsed / 1e6);
}

// 双线程测试 - 每个线程处理一半数组
static void run_dual_thread(int cpu1, int cpu2, const char *desc) {
    printf("\n=== %s ===\n", desc);
    printf("CPU binding: Thread0 -> CPU%d, Thread1 -> CPU%d\n", cpu1, cpu2);

    pthread_t threads[2];
    thread_arg_t args[2];
    volatile int ready = 0;
    volatile int start = 0;

    int half = ELEMENTS / 2;

    // 两个线程处理相邻的内存区域
    // 这意味着它们会共享缓存行边界处的数据
    args[0] = (thread_arg_t){
        .cpu_id = cpu1, .thread_id = 0,
        .start_idx = 0, .end_idx = half,
        .ready = &ready, .start = &start
    };
    args[1] = (thread_arg_t){
        .cpu_id = cpu2, .thread_id = 1,
        .start_idx = half, .end_idx = ELEMENTS,
        .ready = &ready, .start = &start
    };

    pthread_create(&threads[0], NULL, worker_thread, &args[0]);
    pthread_create(&threads[1], NULL, worker_thread, &args[1]);

    while (ready < 2) usleep(100);

    double wall_start = get_time_sec();
    start = 1;

    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    double wall_elapsed = get_time_sec() - wall_start;

    printf("Thread 0: Result=%lu, Time=%.4f sec\n", args[0].result, args[0].elapsed_time);
    printf("Thread 1: Result=%lu, Time=%.4f sec\n", args[1].result, args[1].elapsed_time);
    printf("Wall time: %.4f seconds\n", wall_elapsed);
    printf("Throughput: %.2f M ops/sec\n",
           (double)(ELEMENTS * ITERATIONS) / wall_elapsed / 1e6);
}

int main(int argc, char *argv[]) {
    // 初始化共享数组
    for (size_t i = 0; i < ELEMENTS; i++) {
        shared_array[i] = i;
    }

    printf("=== Shared Cache Cooperation Test ===\n");
    printf("Array size: %d KB (fits in L1 cache)\n", ARRAY_SIZE / 1024);
    printf("Elements: %ld\n", ELEMENTS);
    printf("Iterations: %d\n", ITERATIONS);
    printf("L1 D-Cache: 32 KB (shared by HT siblings)\n");

    const char *mode = argc > 1 ? argv[1] : "--all";

    if (strcmp(mode, "--same-core") == 0) {
        run_dual_thread(0, 8, "Same Core HT (CPU 0,8) - Shared L1 Cache");
    } else if (strcmp(mode, "--diff-core") == 0) {
        run_dual_thread(0, 1, "Different Cores (CPU 0,1) - Separate L1 Caches");
    } else if (strcmp(mode, "--single") == 0) {
        run_single_thread();
    } else if (strcmp(mode, "--all") == 0) {
        run_single_thread();
        run_dual_thread(0, 8, "Same Core HT (CPU 0,8) - Shared L1 Cache");
        run_dual_thread(0, 1, "Different Cores (CPU 0,1) - Separate L1 Caches");

        printf("\n=== Analysis ===\n");
        printf("Expected benefits of same-core HT:\n");
        printf("1. Shared L1 cache - data prefetched by one thread benefits the other\n");
        printf("2. Lower cache-to-cache transfer latency\n");
        printf("3. Better cache utilization for small working sets\n");
    } else {
        printf("Usage: %s [--same-core | --diff-core | --single | --all]\n", argv[0]);
        return 1;
    }

    return 0;
}
