/*
 * false_sharing.c - 伪共享（False Sharing）问题演示
 *
 * 当多个线程访问同一缓存行中的不同变量时，会导致缓存行频繁失效。
 *
 * 编译: gcc -O2 -pthread -o false_sharing false_sharing.c
 * 运行: ./false_sharing [--bad | --good | --all]
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include "../common/cpu_bindind.h"
#include "../common/prefetch_utils.h"

#define NUM_THREADS 4
#define ITERATIONS 100000000

// 坏的设计：所有计数器紧密排列，共享缓存行
struct bad_counters {
    volatile uint64_t counter[NUM_THREADS];  // 全部在同一个或相邻缓存行
};

// 好的设计：每个计数器独占一个缓存行
struct good_counters {
    struct {
        volatile uint64_t counter;
        char padding[CACHE_LINE_SIZE - sizeof(uint64_t)];  // 填充到缓存行大小
    } per_thread[NUM_THREADS];
};

static struct bad_counters bad CACHE_ALIGNED;
static struct good_counters good CACHE_ALIGNED;

typedef struct {
    int thread_id;
    int cpu_id;
    int use_good;  // 1: 使用好的设计, 0: 使用坏的设计
    volatile int *ready;
    volatile int *start;
    double elapsed_time;
} thread_arg_t;

static void *worker_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;
    int id = targ->thread_id;

    bind_to_cpu(targ->cpu_id);

    __atomic_fetch_add(targ->ready, 1, __ATOMIC_SEQ_CST);
    while (*targ->start == 0) {
        __asm__ __volatile__("pause" ::: "memory");
    }

    double start = get_time_sec();

    if (targ->use_good) {
        // 好的设计：每个线程修改独立的缓存行
        for (long i = 0; i < ITERATIONS; i++) {
            good.per_thread[id].counter++;
        }
    } else {
        // 坏的设计：多个线程修改同一缓存行
        for (long i = 0; i < ITERATIONS; i++) {
            bad.counter[id]++;
        }
    }

    targ->elapsed_time = get_time_sec() - start;
    return NULL;
}

static void run_test(int use_good, const char *desc) {
    printf("\n=== %s ===\n", desc);

    pthread_t threads[NUM_THREADS];
    thread_arg_t args[NUM_THREADS];
    volatile int ready = 0;
    volatile int start = 0;

    // 重置计数器
    memset(&bad, 0, sizeof(bad));
    memset(&good, 0, sizeof(good));

    // 使用不同核心的超线程，最大化缓存行竞争
    int cpus[] = {0, 1, 2, 3};  // 不同核心

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i] = (thread_arg_t){
            .thread_id = i,
            .cpu_id = cpus[i],
            .use_good = use_good,
            .ready = &ready,
            .start = &start
        };
        pthread_create(&threads[i], NULL, worker_thread, &args[i]);
    }

    while (ready < NUM_THREADS) usleep(100);

    double wall_start = get_time_sec();
    start = 1;

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    double wall_elapsed = get_time_sec() - wall_start;

    printf("Threads: %d, Iterations per thread: %d\n", NUM_THREADS, ITERATIONS);
    printf("Thread times:\n");
    double max_time = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        printf("  Thread %d: %.4f sec\n", i, args[i].elapsed_time);
        if (args[i].elapsed_time > max_time) max_time = args[i].elapsed_time;
    }
    printf("Wall time: %.4f seconds\n", wall_elapsed);
    printf("Ops/sec: %.2f M\n", (double)(ITERATIONS * NUM_THREADS) / wall_elapsed / 1e6);
}

int main(int argc, char *argv[]) {
    printf("=== False Sharing Demonstration ===\n");
    printf("Cache line size: %d bytes\n", CACHE_LINE_SIZE);
    printf("\n");
    printf("Bad design: All counters share cache line(s)\n");
    printf("  sizeof(bad_counters) = %lu bytes\n", sizeof(bad));
    printf("\n");
    printf("Good design: Each counter has its own cache line\n");
    printf("  sizeof(good_counters) = %lu bytes\n", sizeof(good));

    const char *mode = argc > 1 ? argv[1] : "--all";

    if (strcmp(mode, "--bad") == 0) {
        run_test(0, "Bad Design (False Sharing)");
    } else if (strcmp(mode, "--good") == 0) {
        run_test(1, "Good Design (No False Sharing)");
    } else if (strcmp(mode, "--all") == 0) {
        run_test(0, "Bad Design (False Sharing)");
        run_test(1, "Good Design (No False Sharing)");

        printf("\n=== Analysis ===\n");
        printf("False sharing occurs when:\n");
        printf("- Multiple threads modify different variables\n");
        printf("- But those variables share the same cache line\n");
        printf("- Each write invalidates the line in other cores' caches\n");
        printf("\nSolution:\n");
        printf("- Pad each variable to its own cache line\n");
        printf("- Use __attribute__((aligned(64)))\n");
        printf("- Or use CACHE_PADDED macro\n");
    } else {
        printf("Usage: %s [--bad | --good | --all]\n", argv[0]);
        return 1;
    }

    return 0;
}
