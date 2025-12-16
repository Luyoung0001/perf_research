/*
 * combined_test.c - 超线程 + 预取综合对比测试
 *
 * 测试预取指令是否能缓解超线程缓存竞争，以及两者结合的效果。
 *
 * 编译: gcc -O2 -pthread -o combined_test combined_test.c
 * 运行: ./combined_test
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include "../common/cpu_bindind.h"
#include "../common/prefetch_utils.h"

#define ARRAY_SIZE (32 * 1024 * 1024)  // 32MB per thread
#define PREFETCH_DISTANCE 16

typedef struct {
    int cpu_id;
    int thread_id;
    int use_prefetch;
    uint64_t *array;
    volatile int *ready;
    volatile int *start;
    uint64_t result;
    double elapsed_time;
} thread_arg_t;

// 不使用预取
static uint64_t process_no_prefetch(uint64_t *array, size_t elements) {
    uint64_t sum = 0;
    for (size_t i = 0; i < elements; i++) {
        sum += array[i];
        array[i] = sum & 0xFF;
    }
    return sum;
}

// 使用预取
static uint64_t process_with_prefetch(uint64_t *array, size_t elements) {
    uint64_t sum = 0;
    for (size_t i = 0; i < elements; i++) {
        PREFETCH_T0(&array[i + PREFETCH_DISTANCE]);
        sum += array[i];
        array[i] = sum & 0xFF;
    }
    return sum;
}

static void *worker_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;
    size_t elements = ARRAY_SIZE / sizeof(uint64_t);

    bind_to_cpu(targ->cpu_id);

    __atomic_fetch_add(targ->ready, 1, __ATOMIC_SEQ_CST);
    while (*targ->start == 0) {
        __asm__ __volatile__("pause" ::: "memory");
    }

    double start = get_time_sec();

    if (targ->use_prefetch) {
        targ->result = process_with_prefetch(targ->array, elements);
    } else {
        targ->result = process_no_prefetch(targ->array, elements);
    }

    targ->elapsed_time = get_time_sec() - start;
    return NULL;
}

static double run_single(int use_prefetch) {
    uint64_t *array = aligned_alloc(CACHE_LINE_SIZE, ARRAY_SIZE);
    memset(array, 0x55, ARRAY_SIZE);

    bind_to_cpu(0);

    double start = get_time_sec();
    size_t elements = ARRAY_SIZE / sizeof(uint64_t);

    if (use_prefetch) {
        process_with_prefetch(array, elements);
    } else {
        process_no_prefetch(array, elements);
    }

    double elapsed = get_time_sec() - start;
    free(array);
    return elapsed;
}

static double run_dual(int cpu1, int cpu2, int use_prefetch) {
    uint64_t *array1 = aligned_alloc(CACHE_LINE_SIZE, ARRAY_SIZE);
    uint64_t *array2 = aligned_alloc(CACHE_LINE_SIZE, ARRAY_SIZE);
    memset(array1, 0x55, ARRAY_SIZE);
    memset(array2, 0xAA, ARRAY_SIZE);

    pthread_t threads[2];
    thread_arg_t args[2];
    volatile int ready = 0;
    volatile int start = 0;

    args[0] = (thread_arg_t){
        .cpu_id = cpu1, .thread_id = 0, .use_prefetch = use_prefetch,
        .array = array1, .ready = &ready, .start = &start
    };
    args[1] = (thread_arg_t){
        .cpu_id = cpu2, .thread_id = 1, .use_prefetch = use_prefetch,
        .array = array2, .ready = &ready, .start = &start
    };

    pthread_create(&threads[0], NULL, worker_thread, &args[0]);
    pthread_create(&threads[1], NULL, worker_thread, &args[1]);

    while (ready < 2) usleep(100);

    double wall_start = get_time_sec();
    start = 1;

    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    double wall_elapsed = get_time_sec() - wall_start;

    free(array1);
    free(array2);
    return wall_elapsed;
}

int main(int argc, char *argv[]) {
    printf("=== Combined Hyper-Threading + Prefetch Test ===\n");
    printf("Array size per thread: %d MB\n", ARRAY_SIZE / (1024 * 1024));
    printf("Prefetch distance: %d elements\n\n", PREFETCH_DISTANCE);

    // 运行所有配置
    printf("%-40s %10s %10s\n", "Configuration", "Time(s)", "Speedup");
    printf("------------------------------------------------------------\n");

    // 基准：单线程无预取
    double single_no_pf = run_single(0);
    printf("%-40s %10.4f %10s\n", "Single thread, no prefetch", single_no_pf, "1.00x");

    // 单线程有预取
    double single_pf = run_single(1);
    printf("%-40s %10.4f %10.2fx\n", "Single thread, with prefetch",
           single_pf, single_no_pf / single_pf);

    // 同核心超线程，无预取
    double ht_same_no_pf = run_dual(0, 8, 0);
    printf("%-40s %10.4f %10.2fx\n", "Same core HT (0,8), no prefetch",
           ht_same_no_pf, single_no_pf / ht_same_no_pf);

    // 同核心超线程，有预取
    double ht_same_pf = run_dual(0, 8, 1);
    printf("%-40s %10.4f %10.2fx\n", "Same core HT (0,8), with prefetch",
           ht_same_pf, single_no_pf / ht_same_pf);

    // 不同核心，无预取
    double diff_no_pf = run_dual(0, 1, 0);
    printf("%-40s %10.4f %10.2fx\n", "Different cores (0,1), no prefetch",
           diff_no_pf, single_no_pf / diff_no_pf);

    // 不同核心，有预取
    double diff_pf = run_dual(0, 1, 1);
    printf("%-40s %10.4f %10.2fx\n", "Different cores (0,1), with prefetch",
           diff_pf, single_no_pf / diff_pf);

    printf("\n=== Analysis ===\n");
    printf("Prefetch improvement (single):     %.1f%%\n",
           (single_no_pf / single_pf - 1) * 100);
    printf("HT same core improvement:          %.1f%%\n",
           (single_no_pf / ht_same_no_pf - 1) * 100);
    printf("HT same core + prefetch:           %.1f%%\n",
           (single_no_pf / ht_same_pf - 1) * 100);
    printf("Different cores improvement:       %.1f%%\n",
           (single_no_pf / diff_no_pf - 1) * 100);
    printf("Different cores + prefetch:        %.1f%%\n",
           (single_no_pf / diff_pf - 1) * 100);

    printf("\nKey findings:\n");
    printf("1. Compare HT with/without prefetch to see if prefetch helps\n");
    printf("2. Compare HT vs different cores for parallelism benefit\n");
    printf("3. Best config is usually: different cores + prefetch\n");

    return 0;
}
