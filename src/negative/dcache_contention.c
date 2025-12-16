/*
 * dcache_contention.c - 数据缓存竞争测试
 *
 * 演示同一核心的两个超线程由于访问不同内存区域，
 * 导致 L1 dcache 频繁竞争的负面效果。
 *
 * 编译: gcc -O2 -pthread -o dcache_contention dcache_contention.c
 * 运行: ./dcache_contention [--same-core | --diff-core | --single]
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
#define ARRAY_SIZE (8 * 1024 * 1024)  // 8MB，远大于 L1 cache (32KB)
#define ITERATIONS 10
#define STRIDE 64  // 每次跳过一个缓存行，最大化 cache miss

// 全局数据 - 两个线程访问不同的数组
static uint64_t *array1;
static uint64_t *array2;

// 线程参数
typedef struct {
    int cpu_id;
    int thread_id;
    uint64_t *array;
    volatile int *ready;
    volatile int *start;
    uint64_t result;
    double elapsed_time;
} thread_arg_t;

// 随机访问模式 - 破坏空间局部性
static uint64_t random_access_pattern(uint64_t *array, size_t size) {
    uint64_t sum = 0;
    size_t elements = size / sizeof(uint64_t);

    // 使用大步长访问，确保每次访问不同的缓存行
    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (size_t i = 0; i < elements; i += STRIDE) {
            sum += array[i];
            // 写入操作，增加缓存压力
            array[i] = sum;
        }
    }
    return sum;
}

// 线程工作函数
static void *worker_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;

    // 绑定到指定 CPU
    if (bind_to_cpu(targ->cpu_id) != 0) {
        fprintf(stderr, "Thread %d: Failed to bind to CPU %d\n",
                targ->thread_id, targ->cpu_id);
        return NULL;
    }

    print_cpu_bindind(targ->thread_id == 0 ? "Thread0" : "Thread1");

    // 等待所有线程就绪
    __atomic_fetch_add(targ->ready, 1, __ATOMIC_SEQ_CST);
    while (*targ->start == 0) {
        __asm__ __volatile__("pause" ::: "memory");
    }

    // 开始计时
    double start = get_time_sec();

    // 执行随机访问
    targ->result = random_access_pattern(targ->array, ARRAY_SIZE);

    // 结束计时
    targ->elapsed_time = get_time_sec() - start;

    return NULL;
}

// 单线程测试
static void run_single_thread(void) {
    printf("\n=== Single Thread Test ===\n");

    bind_to_cpu(0);
    print_cpu_bindind("SingleThread");

    double start = get_time_sec();
    uint64_t result = random_access_pattern(array1, ARRAY_SIZE);
    double elapsed = get_time_sec() - start;

    printf("Result: %lu\n", result);
    printf("Time: %.4f seconds\n", elapsed);
}

// 双线程测试
static void run_dual_thread(int cpu1, int cpu2, const char *desc) {
    printf("\n=== %s ===\n", desc);
    printf("CPU binding: Thread0 -> CPU%d, Thread1 -> CPU%d\n", cpu1, cpu2);

    pthread_t threads[2];
    thread_arg_t args[2];
    volatile int ready = 0;
    volatile int start = 0;

    // 初始化线程参数
    args[0] = (thread_arg_t){
        .cpu_id = cpu1,
        .thread_id = 0,
        .array = array1,
        .ready = &ready,
        .start = &start
    };
    args[1] = (thread_arg_t){
        .cpu_id = cpu2,
        .thread_id = 1,
        .array = array2,
        .ready = &ready,
        .start = &start
    };

    // 创建线程
    pthread_create(&threads[0], NULL, worker_thread, &args[0]);
    pthread_create(&threads[1], NULL, worker_thread, &args[1]);

    // 等待线程就绪
    while (ready < 2) {
        usleep(100);
    }

    // 同时启动所有线程
    double wall_start = get_time_sec();
    start = 1;

    // 等待完成
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    double wall_elapsed = get_time_sec() - wall_start;

    // 打印结果
    printf("Thread 0: Result=%lu, Time=%.4f sec\n",
           args[0].result, args[0].elapsed_time);
    printf("Thread 1: Result=%lu, Time=%.4f sec\n",
           args[1].result, args[1].elapsed_time);
    printf("Wall time: %.4f seconds\n", wall_elapsed);
}

static void print_usage(const char *prog) {
    printf("Usage: %s [--same-core | --diff-core | --single | --all]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  --same-core  Two threads on same core (CPU 0,8) - HT siblings\n");
    printf("  --diff-core  Two threads on different cores (CPU 0,1)\n");
    printf("  --single     Single thread baseline\n");
    printf("  --all        Run all tests\n");
}

int main(int argc, char *argv[]) {
    // 分配内存
    array1 = aligned_alloc(CACHE_LINE_SIZE, ARRAY_SIZE);
    array2 = aligned_alloc(CACHE_LINE_SIZE, ARRAY_SIZE);

    if (!array1 || !array2) {
        perror("Memory allocation failed");
        return 1;
    }

    // 初始化数组
    memset(array1, 0x55, ARRAY_SIZE);
    memset(array2, 0xAA, ARRAY_SIZE);

    printf("=== D-Cache Contention Test ===\n");
    printf("Array size: %d MB each\n", ARRAY_SIZE / (1024 * 1024));
    printf("L1 D-Cache: 32 KB (shared by HT siblings)\n");
    printf("Stride: %d elements (%ld bytes)\n", STRIDE, STRIDE * sizeof(uint64_t));
    printf("Iterations: %d\n", ITERATIONS);

    const char *mode = argc > 1 ? argv[1] : "--all";

    if (strcmp(mode, "--same-core") == 0) {
        // 同一核心的两个超线程 (CPU 0 和 8)
        run_dual_thread(0, 8, "Same Core HT (CPU 0,8) - Cache Contention");
    } else if (strcmp(mode, "--diff-core") == 0) {
        // 不同核心 (CPU 0 和 1)
        run_dual_thread(0, 1, "Different Cores (CPU 0,1) - Independent Caches");
    } else if (strcmp(mode, "--single") == 0) {
        run_single_thread();
    } else if (strcmp(mode, "--all") == 0) {
        run_single_thread();
        run_dual_thread(0, 8, "Same Core HT (CPU 0,8) - Cache Contention");
        run_dual_thread(0, 1, "Different Cores (CPU 0,1) - Independent Caches");

        printf("\n=== Analysis ===\n");
        printf("Expected: Same-core HT should be SLOWER due to L1 cache contention\n");
        printf("         Different-core should be faster (independent L1 caches)\n");
    } else {
        print_usage(argv[0]);
        return 1;
    }

    free(array1);
    free(array2);

    return 0;
}
