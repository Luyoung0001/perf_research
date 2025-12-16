/*
 * latency_hiding.c - 延迟隐藏测试
 *
 * 演示超线程如何隐藏内存访问延迟：
 * 当一个线程等待内存时，另一个线程可以使用 CPU 资源。
 *
 * 编译: gcc -O2 -pthread -o latency_hiding latency_hiding.c -lm
 * 运行: ./latency_hiding [--same-core | --diff-core | --single | --all]
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <math.h>
#include "../common/cpu_bindind.h"
#include "../common/prefetch_utils.h"

// 配置参数
#define LARGE_ARRAY_SIZE (64 * 1024 * 1024)  // 64MB - 远超所有缓存
#define COMPUTE_ITERATIONS 10000000
#define MEMORY_ACCESSES 5000000

// 大数组用于内存密集型任务
static uint64_t *large_array;

// 线程类型
typedef enum {
    THREAD_COMPUTE,  // 计算密集型
    THREAD_MEMORY    // 内存密集型
} thread_type_t;

// 线程参数
typedef struct {
    int cpu_id;
    thread_type_t type;
    volatile int *ready;
    volatile int *start;
    uint64_t result;
    double elapsed_time;
} thread_arg_t;

// 计算密集型任务 - 不访问内存，纯 CPU 运算
static uint64_t compute_intensive(void) {
    double result = 1.0;

    for (long i = 0; i < COMPUTE_ITERATIONS; i++) {
        result = sin(result) * cos(result) + sqrt(fabs(result) + 1.0);
        result = log(fabs(result) + 1.0) * exp(-fabs(result) * 0.001);
    }

    return (uint64_t)(result * 1000000);
}

// 内存密集型任务 - 随机访问大数组，产生大量 cache miss
static uint64_t memory_intensive(void) {
    uint64_t sum = 0;
    size_t elements = LARGE_ARRAY_SIZE / sizeof(uint64_t);
    uint64_t seed = 12345;

    for (long i = 0; i < MEMORY_ACCESSES; i++) {
        // 简单的伪随机索引生成
        seed = seed * 1103515245 + 12345;
        size_t idx = (seed >> 16) % elements;

        sum += large_array[idx];
        large_array[idx] = sum;
    }

    return sum;
}

static void *worker_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;

    bind_to_cpu(targ->cpu_id);
    print_cpu_bindind(targ->type == THREAD_COMPUTE ? "Compute" : "Memory");

    __atomic_fetch_add(targ->ready, 1, __ATOMIC_SEQ_CST);
    while (*targ->start == 0) {
        __asm__ __volatile__("pause" ::: "memory");
    }

    double start = get_time_sec();
    targ->result = (targ->type == THREAD_COMPUTE) ?
                   compute_intensive() : memory_intensive();
    targ->elapsed_time = get_time_sec() - start;

    return NULL;
}

// 单线程计算密集型
static void run_single_compute(void) {
    printf("\n=== Single Thread - Compute Intensive ===\n");
    bind_to_cpu(0);

    double start = get_time_sec();
    uint64_t result = compute_intensive();
    double elapsed = get_time_sec() - start;

    printf("Result: %lu, Time: %.4f sec\n", result, elapsed);
}

// 单线程内存密集型
static void run_single_memory(void) {
    printf("\n=== Single Thread - Memory Intensive ===\n");
    bind_to_cpu(0);

    double start = get_time_sec();
    uint64_t result = memory_intensive();
    double elapsed = get_time_sec() - start;

    printf("Result: %lu, Time: %.4f sec\n", result, elapsed);
}

// 单线程串行执行两个任务
static void run_single_both(void) {
    printf("\n=== Single Thread - Both Tasks Serial ===\n");
    bind_to_cpu(0);

    double start = get_time_sec();
    uint64_t r1 = compute_intensive();
    uint64_t r2 = memory_intensive();
    double elapsed = get_time_sec() - start;

    printf("Compute result: %lu\n", r1);
    printf("Memory result: %lu\n", r2);
    printf("Total time: %.4f sec\n", elapsed);
}

// 双线程并行执行
static void run_dual_thread(int cpu1, int cpu2, const char *desc) {
    printf("\n=== %s ===\n", desc);
    printf("Compute thread -> CPU%d, Memory thread -> CPU%d\n", cpu1, cpu2);

    pthread_t threads[2];
    thread_arg_t args[2];
    volatile int ready = 0;
    volatile int start = 0;

    args[0] = (thread_arg_t){
        .cpu_id = cpu1, .type = THREAD_COMPUTE,
        .ready = &ready, .start = &start
    };
    args[1] = (thread_arg_t){
        .cpu_id = cpu2, .type = THREAD_MEMORY,
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

    printf("Compute: Result=%lu, Time=%.4f sec\n", args[0].result, args[0].elapsed_time);
    printf("Memory:  Result=%lu, Time=%.4f sec\n", args[1].result, args[1].elapsed_time);
    printf("Wall time: %.4f seconds\n", wall_elapsed);
}

int main(int argc, char *argv[]) {
    // 分配大数组
    large_array = aligned_alloc(CACHE_LINE_SIZE, LARGE_ARRAY_SIZE);
    if (!large_array) {
        perror("Memory allocation failed");
        return 1;
    }

    // 初始化
    memset(large_array, 0x55, LARGE_ARRAY_SIZE);

    printf("=== Latency Hiding Test ===\n");
    printf("Large array: %d MB\n", LARGE_ARRAY_SIZE / (1024 * 1024));
    printf("Compute iterations: %d\n", COMPUTE_ITERATIONS);
    printf("Memory accesses: %d\n", MEMORY_ACCESSES);
    printf("\nHypothesis:\n");
    printf("- HT on same core: Memory thread stalls -> Compute thread uses CPU\n");
    printf("- This 'latency hiding' should improve total throughput\n");

    const char *mode = argc > 1 ? argv[1] : "--all";

    if (strcmp(mode, "--same-core") == 0) {
        run_dual_thread(0, 8, "Same Core HT (CPU 0,8) - Latency Hiding");
    } else if (strcmp(mode, "--diff-core") == 0) {
        run_dual_thread(0, 1, "Different Cores (CPU 0,1)");
    } else if (strcmp(mode, "--single") == 0) {
        run_single_both();
    } else if (strcmp(mode, "--all") == 0) {
        run_single_compute();
        run_single_memory();
        run_single_both();
        run_dual_thread(0, 8, "Same Core HT (CPU 0,8) - Latency Hiding");
        run_dual_thread(0, 1, "Different Cores (CPU 0,1) - Full Parallelism");

        printf("\n=== Analysis ===\n");
        printf("Compare 'Single Both' time with 'Same Core HT' wall time:\n");
        printf("- If HT is faster: Latency hiding is effective\n");
        printf("- Memory thread stalls on cache misses allow compute thread to run\n");
        printf("\n");
        printf("Compare 'Same Core HT' with 'Different Cores':\n");
        printf("- Different cores should be fastest (true parallelism)\n");
        printf("- Same core HT trades off resources but hides latency\n");
    } else {
        printf("Usage: %s [--same-core | --diff-core | --single | --all]\n", argv[0]);
        free(large_array);
        return 1;
    }

    free(large_array);
    return 0;
}
