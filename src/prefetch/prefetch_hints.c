/*
 * prefetch_hints.c - 预取提示类型对比测试
 *
 * 对比不同预取提示类型（T0, T1, T2, NTA）的效果。
 *
 * 编译: gcc -O2 -o prefetch_hints prefetch_hints.c
 * 运行: ./prefetch_hints
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../common/cpu_bindind.h"
#include "../common/prefetch_utils.h"

#define ARRAY_SIZE (128 * 1024 * 1024)  // 128MB
#define ITERATIONS 3
#define PREFETCH_DISTANCE 16

static uint64_t *array;

// 无预取
static uint64_t no_prefetch(void) {
    uint64_t sum = 0;
    size_t elements = ARRAY_SIZE / sizeof(uint64_t);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (size_t i = 0; i < elements; i++) {
            sum += array[i];
        }
    }
    return sum;
}

// T0 预取 - 预取到所有缓存级别
static uint64_t prefetch_t0(void) {
    uint64_t sum = 0;
    size_t elements = ARRAY_SIZE / sizeof(uint64_t);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (size_t i = 0; i < elements; i++) {
            PREFETCH_T0(&array[i + PREFETCH_DISTANCE]);
            sum += array[i];
        }
    }
    return sum;
}

// T1 预取 - 预取到 L2 及以上
static uint64_t prefetch_t1(void) {
    uint64_t sum = 0;
    size_t elements = ARRAY_SIZE / sizeof(uint64_t);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (size_t i = 0; i < elements; i++) {
            PREFETCH_T1(&array[i + PREFETCH_DISTANCE]);
            sum += array[i];
        }
    }
    return sum;
}

// T2 预取 - 预取到 L3 及以上
static uint64_t prefetch_t2(void) {
    uint64_t sum = 0;
    size_t elements = ARRAY_SIZE / sizeof(uint64_t);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (size_t i = 0; i < elements; i++) {
            PREFETCH_T2(&array[i + PREFETCH_DISTANCE]);
            sum += array[i];
        }
    }
    return sum;
}

// NTA 预取 - 非临时访问
static uint64_t prefetch_nta(void) {
    uint64_t sum = 0;
    size_t elements = ARRAY_SIZE / sizeof(uint64_t);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (size_t i = 0; i < elements; i++) {
            PREFETCH_NTA(&array[i + PREFETCH_DISTANCE]);
            sum += array[i];
        }
    }
    return sum;
}

static void flush_cache(void) {
    for (size_t i = 0; i < ARRAY_SIZE / sizeof(uint64_t); i += 8) {
        CLFLUSH(&array[i]);
    }
    BARRIER();
}

static void run_test(const char *name, uint64_t (*func)(void)) {
    flush_cache();

    double start = get_time_sec();
    uint64_t result = func();
    double elapsed = get_time_sec() - start;

    size_t total_bytes = (size_t)ARRAY_SIZE * ITERATIONS;
    double bandwidth = total_bytes / elapsed / (1024.0 * 1024 * 1024);

    printf("%-20s: Time=%.4fs, BW=%.2f GB/s (result=%lu)\n",
           name, elapsed, bandwidth, result % 1000);
}

int main(int argc, char *argv[]) {
    array = aligned_alloc(CACHE_LINE_SIZE, ARRAY_SIZE);
    if (!array) {
        perror("Memory allocation failed");
        return 1;
    }

    for (size_t i = 0; i < ARRAY_SIZE / sizeof(uint64_t); i++) {
        array[i] = i;
    }

    bind_to_cpu(0);

    printf("=== Prefetch Hints Comparison ===\n");
    printf("Array size: %d MB\n", ARRAY_SIZE / (1024 * 1024));
    printf("Iterations: %d\n", ITERATIONS);
    printf("Prefetch distance: %d elements\n\n", PREFETCH_DISTANCE);

    printf("Hint types:\n");
    printf("  T0  - Prefetch to all cache levels (L1, L2, L3)\n");
    printf("  T1  - Prefetch to L2 and above\n");
    printf("  T2  - Prefetch to L3 and above\n");
    printf("  NTA - Non-temporal (minimize cache pollution)\n\n");

    run_test("No Prefetch", no_prefetch);
    run_test("Prefetch T0 (L1)", prefetch_t0);
    run_test("Prefetch T1 (L2)", prefetch_t1);
    run_test("Prefetch T2 (L3)", prefetch_t2);
    run_test("Prefetch NTA", prefetch_nta);

    printf("\n=== Analysis ===\n");
    printf("T0: Best for data that will be reused soon\n");
    printf("    Brings data closest to CPU (L1)\n\n");
    printf("T1/T2: Good for data with delayed reuse\n");
    printf("    Avoids polluting L1 cache\n\n");
    printf("NTA: Best for streaming data (read once)\n");
    printf("    Minimizes cache pollution\n");
    printf("    Data bypasses or quickly evicts from cache\n\n");
    printf("For sequential streaming, NTA often performs best\n");
    printf("because it doesn't pollute the cache with data\n");
    printf("that won't be reused.\n");

    free(array);
    return 0;
}
