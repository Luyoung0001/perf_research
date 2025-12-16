/*
 * sequential_prefetch.c - 顺序访问预取测试
 *
 * 对比有无预取指令对顺序内存访问的性能影响。
 *
 * 编译: gcc -O2 -o sequential_prefetch sequential_prefetch.c
 * 运行: ./sequential_prefetch [--no-prefetch | --prefetch | --all]
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../common/cpu_bindind.h"
#include "../common/prefetch_utils.h"

// 配置参数
#define ARRAY_SIZE (128 * 1024 * 1024)  // 128MB - 远超所有缓存
#define ITERATIONS 5
#define PREFETCH_DISTANCE 16  // 预取距离（元素个数）

static uint64_t *array;

// 无预取的顺序访问
static uint64_t sequential_no_prefetch(void) {
    uint64_t sum = 0;
    size_t elements = ARRAY_SIZE / sizeof(uint64_t);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (size_t i = 0; i < elements; i++) {
            sum += array[i];
        }
    }

    return sum;
}

// 有预取的顺序访问
static uint64_t sequential_with_prefetch(void) {
    uint64_t sum = 0;
    size_t elements = ARRAY_SIZE / sizeof(uint64_t);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (size_t i = 0; i < elements; i++) {
            // 预取后续数据
            PREFETCH_T0(&array[i + PREFETCH_DISTANCE]);
            sum += array[i];
        }
    }

    return sum;
}

// 使用 NTA 预取（非临时访问，绕过缓存）
static uint64_t sequential_with_prefetch_nta(void) {
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

static void run_test(const char *name,
                     uint64_t (*test_func)(void)) {
    printf("\n=== %s ===\n", name);

    // 冷启动 - 清除缓存影响
    for (size_t i = 0; i < ARRAY_SIZE / sizeof(uint64_t); i += 64) {
        CLFLUSH(&array[i]);
    }
    BARRIER();

    double start = get_time_sec();
    uint64_t result = test_func();
    double elapsed = get_time_sec() - start;

    size_t total_bytes = (size_t)ARRAY_SIZE * ITERATIONS;
    double bandwidth = total_bytes / elapsed / (1024.0 * 1024 * 1024);

    printf("Result: %lu\n", result);
    printf("Time: %.4f seconds\n", elapsed);
    printf("Bandwidth: %.2f GB/s\n", bandwidth);
}

int main(int argc, char *argv[]) {
    // 分配对齐内存
    array = aligned_alloc(CACHE_LINE_SIZE, ARRAY_SIZE);
    if (!array) {
        perror("Memory allocation failed");
        return 1;
    }

    // 初始化数组
    for (size_t i = 0; i < ARRAY_SIZE / sizeof(uint64_t); i++) {
        array[i] = i;
    }

    // 绑定到固定 CPU
    bind_to_cpu(0);

    printf("=== Sequential Access Prefetch Test ===\n");
    printf("Array size: %d MB\n", ARRAY_SIZE / (1024 * 1024));
    printf("Iterations: %d\n", ITERATIONS);
    printf("Prefetch distance: %d elements (%ld bytes)\n",
           PREFETCH_DISTANCE, PREFETCH_DISTANCE * sizeof(uint64_t));

    const char *mode = argc > 1 ? argv[1] : "--all";

    if (strcmp(mode, "--no-prefetch") == 0) {
        run_test("No Prefetch", sequential_no_prefetch);
    } else if (strcmp(mode, "--prefetch") == 0) {
        run_test("With Prefetch (T0)", sequential_with_prefetch);
    } else if (strcmp(mode, "--prefetch-nta") == 0) {
        run_test("With Prefetch (NTA)", sequential_with_prefetch_nta);
    } else if (strcmp(mode, "--all") == 0) {
        run_test("No Prefetch (baseline)", sequential_no_prefetch);
        run_test("With Prefetch (T0 - all cache levels)", sequential_with_prefetch);
        run_test("With Prefetch (NTA - non-temporal)", sequential_with_prefetch_nta);

        printf("\n=== Analysis ===\n");
        printf("For sequential access, hardware prefetcher is usually effective.\n");
        printf("Software prefetch may provide marginal benefit or overhead.\n");
        printf("NTA hint can be better for streaming data (avoids cache pollution).\n");
    } else {
        printf("Usage: %s [--no-prefetch | --prefetch | --prefetch-nta | --all]\n", argv[0]);
        free(array);
        return 1;
    }

    free(array);
    return 0;
}
