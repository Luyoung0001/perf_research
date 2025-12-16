/*
 * prefetch_distance.c - 预取距离对比测试
 *
 * 测试不同预取距离对性能的影响。
 * 预取过早：数据可能在使用前被逐出缓存
 * 预取过晚：数据未能及时加载
 *
 * 编译: gcc -O2 -o prefetch_distance prefetch_distance.c
 * 运行: ./prefetch_distance
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../common/cpu_bindind.h"
#include "../common/prefetch_utils.h"

#define ARRAY_SIZE (64 * 1024 * 1024)  // 64MB
#define ACCESS_COUNT 5000000

static uint64_t *array;
static size_t *indices;

// 生成随机索引
static void generate_indices(void) {
    size_t elements = ARRAY_SIZE / sizeof(uint64_t);
    uint64_t seed = 54321;

    for (size_t i = 0; i < ACCESS_COUNT + 256; i++) {
        seed = seed * 1103515245 + 12345;
        indices[i] = (seed >> 16) % elements;
    }
}

// 使用指定预取距离进行随机访问
static uint64_t random_access_with_distance(int distance) {
    uint64_t sum = 0;

    for (size_t i = 0; i < ACCESS_COUNT; i++) {
        if (distance > 0) {
            PREFETCH_T0(&array[indices[i + distance]]);
        }
        sum += array[indices[i]];
    }

    return sum;
}

static void flush_cache(void) {
    for (size_t i = 0; i < ARRAY_SIZE / sizeof(uint64_t); i += 8) {
        CLFLUSH(&array[i]);
    }
    BARRIER();
}

static void test_distance(int distance) {
    flush_cache();

    double start = get_time_sec();
    uint64_t result = random_access_with_distance(distance);
    double elapsed = get_time_sec() - start;

    double throughput = ACCESS_COUNT / elapsed / 1e6;
    double latency = elapsed / ACCESS_COUNT * 1e9;

    printf("Distance %3d: Time=%.4fs, Throughput=%.2f M/s, Latency=%.1f ns (result=%lu)\n",
           distance, elapsed, throughput, latency, result % 1000);
}

int main(int argc, char *argv[]) {
    array = aligned_alloc(CACHE_LINE_SIZE, ARRAY_SIZE);
    indices = malloc((ACCESS_COUNT + 256) * sizeof(size_t));

    if (!array || !indices) {
        perror("Memory allocation failed");
        return 1;
    }

    for (size_t i = 0; i < ARRAY_SIZE / sizeof(uint64_t); i++) {
        array[i] = i;
    }
    generate_indices();

    bind_to_cpu(0);

    printf("=== Prefetch Distance Test ===\n");
    printf("Array size: %d MB\n", ARRAY_SIZE / (1024 * 1024));
    printf("Access count: %d (random)\n", ACCESS_COUNT);
    printf("\nTesting different prefetch distances...\n\n");

    // 测试不同的预取距离
    int distances[] = {0, 1, 2, 4, 8, 16, 32, 64, 128, 256};
    int num_distances = sizeof(distances) / sizeof(distances[0]);

    printf("%-12s %-10s %-15s %-12s\n", "Distance", "Time(s)", "Throughput(M/s)", "Latency(ns)");
    printf("----------------------------------------------------\n");

    for (int i = 0; i < num_distances; i++) {
        test_distance(distances[i]);
    }

    printf("\n=== Analysis ===\n");
    printf("Distance 0: No prefetch (baseline)\n");
    printf("\n");
    printf("Too small (1-2): Prefetch doesn't complete before data is needed\n");
    printf("  - Memory latency not hidden\n");
    printf("\n");
    printf("Optimal (8-32): Prefetch completes just in time\n");
    printf("  - Best latency hiding\n");
    printf("  - Typical sweet spot for most workloads\n");
    printf("\n");
    printf("Too large (64+): Data may be evicted before use\n");
    printf("  - Wastes cache space\n");
    printf("  - May cause extra cache misses\n");
    printf("\n");
    printf("Optimal distance depends on:\n");
    printf("  - Memory latency (~100ns for DRAM)\n");
    printf("  - Loop iteration time\n");
    printf("  - Cache size and replacement policy\n");

    free(array);
    free(indices);
    return 0;
}
