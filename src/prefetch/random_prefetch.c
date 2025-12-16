/*
 * random_prefetch.c - 随机访问预取测试
 *
 * 对比有无预取指令对随机内存访问的性能影响。
 * 这是软件预取真正能发挥作用的场景。
 *
 * 编译: gcc -O2 -o random_prefetch random_prefetch.c
 * 运行: ./random_prefetch [--no-prefetch | --prefetch | --all]
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../common/cpu_bindind.h"
#include "../common/prefetch_utils.h"

// 配置参数
#define ARRAY_SIZE (64 * 1024 * 1024)  // 64MB
#define ACCESS_COUNT 10000000
#define PREFETCH_AHEAD 8  // 预取多少步之后的数据

static uint64_t *array;
static size_t *indices;  // 预生成的随机索引

// 生成随机索引序列
static void generate_random_indices(void) {
    size_t elements = ARRAY_SIZE / sizeof(uint64_t);
    uint64_t seed = 12345;

    for (size_t i = 0; i < ACCESS_COUNT + PREFETCH_AHEAD; i++) {
        seed = seed * 1103515245 + 12345;
        indices[i] = (seed >> 16) % elements;
    }
}

// 无预取的随机访问
static uint64_t random_no_prefetch(void) {
    uint64_t sum = 0;

    for (size_t i = 0; i < ACCESS_COUNT; i++) {
        sum += array[indices[i]];
    }

    return sum;
}

// 有预取的随机访问
static uint64_t random_with_prefetch(void) {
    uint64_t sum = 0;

    for (size_t i = 0; i < ACCESS_COUNT; i++) {
        // 预取未来要访问的数据
        PREFETCH_T0(&array[indices[i + PREFETCH_AHEAD]]);
        sum += array[indices[i]];
    }

    return sum;
}

// 多级预取
static uint64_t random_with_multi_prefetch(void) {
    uint64_t sum = 0;

    for (size_t i = 0; i < ACCESS_COUNT; i++) {
        // 近距离预取到 L1
        PREFETCH_T0(&array[indices[i + 4]]);
        // 远距离预取到 L2
        PREFETCH_T1(&array[indices[i + 16]]);
        sum += array[indices[i]];
    }

    return sum;
}

static void run_test(const char *name, uint64_t (*test_func)(void)) {
    printf("\n=== %s ===\n", name);

    // 冷启动
    for (size_t i = 0; i < ARRAY_SIZE / sizeof(uint64_t); i += 64) {
        CLFLUSH(&array[i]);
    }
    BARRIER();

    double start = get_time_sec();
    uint64_t result = test_func();
    double elapsed = get_time_sec() - start;

    double accesses_per_sec = ACCESS_COUNT / elapsed / 1e6;

    printf("Result: %lu\n", result);
    printf("Time: %.4f seconds\n", elapsed);
    printf("Throughput: %.2f M accesses/sec\n", accesses_per_sec);
    printf("Avg latency: %.1f ns/access\n", elapsed / ACCESS_COUNT * 1e9);
}

int main(int argc, char *argv[]) {
    // 分配内存
    array = aligned_alloc(CACHE_LINE_SIZE, ARRAY_SIZE);
    indices = malloc((ACCESS_COUNT + PREFETCH_AHEAD + 1) * sizeof(size_t));

    if (!array || !indices) {
        perror("Memory allocation failed");
        return 1;
    }

    // 初始化
    for (size_t i = 0; i < ARRAY_SIZE / sizeof(uint64_t); i++) {
        array[i] = i;
    }
    generate_random_indices();

    bind_to_cpu(0);

    printf("=== Random Access Prefetch Test ===\n");
    printf("Array size: %d MB\n", ARRAY_SIZE / (1024 * 1024));
    printf("Access count: %d\n", ACCESS_COUNT);
    printf("Prefetch ahead: %d steps\n", PREFETCH_AHEAD);
    printf("\nThis is where software prefetch shines!\n");
    printf("Hardware prefetcher cannot predict random access patterns.\n");

    const char *mode = argc > 1 ? argv[1] : "--all";

    if (strcmp(mode, "--no-prefetch") == 0) {
        run_test("No Prefetch", random_no_prefetch);
    } else if (strcmp(mode, "--prefetch") == 0) {
        run_test("With Prefetch", random_with_prefetch);
    } else if (strcmp(mode, "--multi-prefetch") == 0) {
        run_test("Multi-level Prefetch", random_with_multi_prefetch);
    } else if (strcmp(mode, "--all") == 0) {
        run_test("No Prefetch (baseline)", random_no_prefetch);
        run_test("With Prefetch (single)", random_with_prefetch);
        run_test("Multi-level Prefetch (T0+T1)", random_with_multi_prefetch);

        printf("\n=== Analysis ===\n");
        printf("For random access:\n");
        printf("- Hardware prefetcher is ineffective\n");
        printf("- Software prefetch can significantly reduce latency\n");
        printf("- Key: prefetch far enough ahead to hide memory latency\n");
        printf("- But not too far, or data may be evicted before use\n");
    } else {
        printf("Usage: %s [--no-prefetch | --prefetch | --multi-prefetch | --all]\n", argv[0]);
        free(array);
        free(indices);
        return 1;
    }

    free(array);
    free(indices);
    return 0;
}
