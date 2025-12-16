/*
 * icache_contention.c - 指令缓存竞争测试
 *
 * 演示同一核心的两个超线程执行完全不同的代码路径，
 * 导致 L1 icache 频繁竞争的负面效果。
 *
 * 编译: gcc -O2 -pthread -o icache_contention icache_contention.c
 * 运行: ./icache_contention [--same-core | --diff-core | --single | --all]
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include "../common/cpu_bindind.h"

#define ITERATIONS 50000000

// 生成大量不同的函数，增加代码量，压满 icache
// 每个函数都有独特的代码路径

#define DEFINE_FUNC_GROUP_A(n) \
    __attribute__((noinline)) uint64_t func_a_##n(uint64_t x) { \
        uint64_t y = x * 17 + n; \
        y = (y << 3) ^ (y >> 5); \
        y += n * 31; \
        y = (y * 0x123456789ULL) ^ n; \
        y = (y << 7) | (y >> 57); \
        return y + n * 13; \
    }

#define DEFINE_FUNC_GROUP_B(n) \
    __attribute__((noinline)) uint64_t func_b_##n(uint64_t x) { \
        uint64_t y = x + n * 23; \
        y = (y >> 4) ^ (y << 6); \
        y -= n * 17; \
        y = (y * 0x987654321ULL) + n; \
        y = (y >> 8) | (y << 56); \
        return y - n * 11; \
    }

// 定义 100 个 A 组函数
DEFINE_FUNC_GROUP_A(0)  DEFINE_FUNC_GROUP_A(1)  DEFINE_FUNC_GROUP_A(2)  DEFINE_FUNC_GROUP_A(3)
DEFINE_FUNC_GROUP_A(4)  DEFINE_FUNC_GROUP_A(5)  DEFINE_FUNC_GROUP_A(6)  DEFINE_FUNC_GROUP_A(7)
DEFINE_FUNC_GROUP_A(8)  DEFINE_FUNC_GROUP_A(9)  DEFINE_FUNC_GROUP_A(10) DEFINE_FUNC_GROUP_A(11)
DEFINE_FUNC_GROUP_A(12) DEFINE_FUNC_GROUP_A(13) DEFINE_FUNC_GROUP_A(14) DEFINE_FUNC_GROUP_A(15)
DEFINE_FUNC_GROUP_A(16) DEFINE_FUNC_GROUP_A(17) DEFINE_FUNC_GROUP_A(18) DEFINE_FUNC_GROUP_A(19)
DEFINE_FUNC_GROUP_A(20) DEFINE_FUNC_GROUP_A(21) DEFINE_FUNC_GROUP_A(22) DEFINE_FUNC_GROUP_A(23)
DEFINE_FUNC_GROUP_A(24) DEFINE_FUNC_GROUP_A(25) DEFINE_FUNC_GROUP_A(26) DEFINE_FUNC_GROUP_A(27)
DEFINE_FUNC_GROUP_A(28) DEFINE_FUNC_GROUP_A(29) DEFINE_FUNC_GROUP_A(30) DEFINE_FUNC_GROUP_A(31)
DEFINE_FUNC_GROUP_A(32) DEFINE_FUNC_GROUP_A(33) DEFINE_FUNC_GROUP_A(34) DEFINE_FUNC_GROUP_A(35)
DEFINE_FUNC_GROUP_A(36) DEFINE_FUNC_GROUP_A(37) DEFINE_FUNC_GROUP_A(38) DEFINE_FUNC_GROUP_A(39)
DEFINE_FUNC_GROUP_A(40) DEFINE_FUNC_GROUP_A(41) DEFINE_FUNC_GROUP_A(42) DEFINE_FUNC_GROUP_A(43)
DEFINE_FUNC_GROUP_A(44) DEFINE_FUNC_GROUP_A(45) DEFINE_FUNC_GROUP_A(46) DEFINE_FUNC_GROUP_A(47)
DEFINE_FUNC_GROUP_A(48) DEFINE_FUNC_GROUP_A(49) DEFINE_FUNC_GROUP_A(50) DEFINE_FUNC_GROUP_A(51)
DEFINE_FUNC_GROUP_A(52) DEFINE_FUNC_GROUP_A(53) DEFINE_FUNC_GROUP_A(54) DEFINE_FUNC_GROUP_A(55)
DEFINE_FUNC_GROUP_A(56) DEFINE_FUNC_GROUP_A(57) DEFINE_FUNC_GROUP_A(58) DEFINE_FUNC_GROUP_A(59)
DEFINE_FUNC_GROUP_A(60) DEFINE_FUNC_GROUP_A(61) DEFINE_FUNC_GROUP_A(62) DEFINE_FUNC_GROUP_A(63)
DEFINE_FUNC_GROUP_A(64) DEFINE_FUNC_GROUP_A(65) DEFINE_FUNC_GROUP_A(66) DEFINE_FUNC_GROUP_A(67)
DEFINE_FUNC_GROUP_A(68) DEFINE_FUNC_GROUP_A(69) DEFINE_FUNC_GROUP_A(70) DEFINE_FUNC_GROUP_A(71)
DEFINE_FUNC_GROUP_A(72) DEFINE_FUNC_GROUP_A(73) DEFINE_FUNC_GROUP_A(74) DEFINE_FUNC_GROUP_A(75)
DEFINE_FUNC_GROUP_A(76) DEFINE_FUNC_GROUP_A(77) DEFINE_FUNC_GROUP_A(78) DEFINE_FUNC_GROUP_A(79)
DEFINE_FUNC_GROUP_A(80) DEFINE_FUNC_GROUP_A(81) DEFINE_FUNC_GROUP_A(82) DEFINE_FUNC_GROUP_A(83)
DEFINE_FUNC_GROUP_A(84) DEFINE_FUNC_GROUP_A(85) DEFINE_FUNC_GROUP_A(86) DEFINE_FUNC_GROUP_A(87)
DEFINE_FUNC_GROUP_A(88) DEFINE_FUNC_GROUP_A(89) DEFINE_FUNC_GROUP_A(90) DEFINE_FUNC_GROUP_A(91)
DEFINE_FUNC_GROUP_A(92) DEFINE_FUNC_GROUP_A(93) DEFINE_FUNC_GROUP_A(94) DEFINE_FUNC_GROUP_A(95)
DEFINE_FUNC_GROUP_A(96) DEFINE_FUNC_GROUP_A(97) DEFINE_FUNC_GROUP_A(98) DEFINE_FUNC_GROUP_A(99)

// 定义 100 个 B 组函数 (完全不同的代码)
DEFINE_FUNC_GROUP_B(0)  DEFINE_FUNC_GROUP_B(1)  DEFINE_FUNC_GROUP_B(2)  DEFINE_FUNC_GROUP_B(3)
DEFINE_FUNC_GROUP_B(4)  DEFINE_FUNC_GROUP_B(5)  DEFINE_FUNC_GROUP_B(6)  DEFINE_FUNC_GROUP_B(7)
DEFINE_FUNC_GROUP_B(8)  DEFINE_FUNC_GROUP_B(9)  DEFINE_FUNC_GROUP_B(10) DEFINE_FUNC_GROUP_B(11)
DEFINE_FUNC_GROUP_B(12) DEFINE_FUNC_GROUP_B(13) DEFINE_FUNC_GROUP_B(14) DEFINE_FUNC_GROUP_B(15)
DEFINE_FUNC_GROUP_B(16) DEFINE_FUNC_GROUP_B(17) DEFINE_FUNC_GROUP_B(18) DEFINE_FUNC_GROUP_B(19)
DEFINE_FUNC_GROUP_B(20) DEFINE_FUNC_GROUP_B(21) DEFINE_FUNC_GROUP_B(22) DEFINE_FUNC_GROUP_B(23)
DEFINE_FUNC_GROUP_B(24) DEFINE_FUNC_GROUP_B(25) DEFINE_FUNC_GROUP_B(26) DEFINE_FUNC_GROUP_B(27)
DEFINE_FUNC_GROUP_B(28) DEFINE_FUNC_GROUP_B(29) DEFINE_FUNC_GROUP_B(30) DEFINE_FUNC_GROUP_B(31)
DEFINE_FUNC_GROUP_B(32) DEFINE_FUNC_GROUP_B(33) DEFINE_FUNC_GROUP_B(34) DEFINE_FUNC_GROUP_B(35)
DEFINE_FUNC_GROUP_B(36) DEFINE_FUNC_GROUP_B(37) DEFINE_FUNC_GROUP_B(38) DEFINE_FUNC_GROUP_B(39)
DEFINE_FUNC_GROUP_B(40) DEFINE_FUNC_GROUP_B(41) DEFINE_FUNC_GROUP_B(42) DEFINE_FUNC_GROUP_B(43)
DEFINE_FUNC_GROUP_B(44) DEFINE_FUNC_GROUP_B(45) DEFINE_FUNC_GROUP_B(46) DEFINE_FUNC_GROUP_B(47)
DEFINE_FUNC_GROUP_B(48) DEFINE_FUNC_GROUP_B(49) DEFINE_FUNC_GROUP_B(50) DEFINE_FUNC_GROUP_B(51)
DEFINE_FUNC_GROUP_B(52) DEFINE_FUNC_GROUP_B(53) DEFINE_FUNC_GROUP_B(54) DEFINE_FUNC_GROUP_B(55)
DEFINE_FUNC_GROUP_B(56) DEFINE_FUNC_GROUP_B(57) DEFINE_FUNC_GROUP_B(58) DEFINE_FUNC_GROUP_B(59)
DEFINE_FUNC_GROUP_B(60) DEFINE_FUNC_GROUP_B(61) DEFINE_FUNC_GROUP_B(62) DEFINE_FUNC_GROUP_B(63)
DEFINE_FUNC_GROUP_B(64) DEFINE_FUNC_GROUP_B(65) DEFINE_FUNC_GROUP_B(66) DEFINE_FUNC_GROUP_B(67)
DEFINE_FUNC_GROUP_B(68) DEFINE_FUNC_GROUP_B(69) DEFINE_FUNC_GROUP_B(70) DEFINE_FUNC_GROUP_B(71)
DEFINE_FUNC_GROUP_B(72) DEFINE_FUNC_GROUP_B(73) DEFINE_FUNC_GROUP_B(74) DEFINE_FUNC_GROUP_B(75)
DEFINE_FUNC_GROUP_B(76) DEFINE_FUNC_GROUP_B(77) DEFINE_FUNC_GROUP_B(78) DEFINE_FUNC_GROUP_B(79)
DEFINE_FUNC_GROUP_B(80) DEFINE_FUNC_GROUP_B(81) DEFINE_FUNC_GROUP_B(82) DEFINE_FUNC_GROUP_B(83)
DEFINE_FUNC_GROUP_B(84) DEFINE_FUNC_GROUP_B(85) DEFINE_FUNC_GROUP_B(86) DEFINE_FUNC_GROUP_B(87)
DEFINE_FUNC_GROUP_B(88) DEFINE_FUNC_GROUP_B(89) DEFINE_FUNC_GROUP_B(90) DEFINE_FUNC_GROUP_B(91)
DEFINE_FUNC_GROUP_B(92) DEFINE_FUNC_GROUP_B(93) DEFINE_FUNC_GROUP_B(94) DEFINE_FUNC_GROUP_B(95)
DEFINE_FUNC_GROUP_B(96) DEFINE_FUNC_GROUP_B(97) DEFINE_FUNC_GROUP_B(98) DEFINE_FUNC_GROUP_B(99)

// 函数指针数组
typedef uint64_t (*func_ptr)(uint64_t);

static func_ptr funcs_a[100] = {
    func_a_0,  func_a_1,  func_a_2,  func_a_3,  func_a_4,  func_a_5,  func_a_6,  func_a_7,
    func_a_8,  func_a_9,  func_a_10, func_a_11, func_a_12, func_a_13, func_a_14, func_a_15,
    func_a_16, func_a_17, func_a_18, func_a_19, func_a_20, func_a_21, func_a_22, func_a_23,
    func_a_24, func_a_25, func_a_26, func_a_27, func_a_28, func_a_29, func_a_30, func_a_31,
    func_a_32, func_a_33, func_a_34, func_a_35, func_a_36, func_a_37, func_a_38, func_a_39,
    func_a_40, func_a_41, func_a_42, func_a_43, func_a_44, func_a_45, func_a_46, func_a_47,
    func_a_48, func_a_49, func_a_50, func_a_51, func_a_52, func_a_53, func_a_54, func_a_55,
    func_a_56, func_a_57, func_a_58, func_a_59, func_a_60, func_a_61, func_a_62, func_a_63,
    func_a_64, func_a_65, func_a_66, func_a_67, func_a_68, func_a_69, func_a_70, func_a_71,
    func_a_72, func_a_73, func_a_74, func_a_75, func_a_76, func_a_77, func_a_78, func_a_79,
    func_a_80, func_a_81, func_a_82, func_a_83, func_a_84, func_a_85, func_a_86, func_a_87,
    func_a_88, func_a_89, func_a_90, func_a_91, func_a_92, func_a_93, func_a_94, func_a_95,
    func_a_96, func_a_97, func_a_98, func_a_99
};

static func_ptr funcs_b[100] = {
    func_b_0,  func_b_1,  func_b_2,  func_b_3,  func_b_4,  func_b_5,  func_b_6,  func_b_7,
    func_b_8,  func_b_9,  func_b_10, func_b_11, func_b_12, func_b_13, func_b_14, func_b_15,
    func_b_16, func_b_17, func_b_18, func_b_19, func_b_20, func_b_21, func_b_22, func_b_23,
    func_b_24, func_b_25, func_b_26, func_b_27, func_b_28, func_b_29, func_b_30, func_b_31,
    func_b_32, func_b_33, func_b_34, func_b_35, func_b_36, func_b_37, func_b_38, func_b_39,
    func_b_40, func_b_41, func_b_42, func_b_43, func_b_44, func_b_45, func_b_46, func_b_47,
    func_b_48, func_b_49, func_b_50, func_b_51, func_b_52, func_b_53, func_b_54, func_b_55,
    func_b_56, func_b_57, func_b_58, func_b_59, func_b_60, func_b_61, func_b_62, func_b_63,
    func_b_64, func_b_65, func_b_66, func_b_67, func_b_68, func_b_69, func_b_70, func_b_71,
    func_b_72, func_b_73, func_b_74, func_b_75, func_b_76, func_b_77, func_b_78, func_b_79,
    func_b_80, func_b_81, func_b_82, func_b_83, func_b_84, func_b_85, func_b_86, func_b_87,
    func_b_88, func_b_89, func_b_90, func_b_91, func_b_92, func_b_93, func_b_94, func_b_95,
    func_b_96, func_b_97, func_b_98, func_b_99
};

// 执行 A 组函数
static uint64_t run_func_group_a(void) {
    uint64_t result = 1;
    for (long i = 0; i < ITERATIONS; i++) {
        int idx = i % 100;
        result = funcs_a[idx](result);
    }
    return result;
}

// 执行 B 组函数
static uint64_t run_func_group_b(void) {
    uint64_t result = 1;
    for (long i = 0; i < ITERATIONS; i++) {
        int idx = i % 100;
        result = funcs_b[idx](result);
    }
    return result;
}

// 线程参数
typedef struct {
    int cpu_id;
    int use_group_a;  // 1: 使用 A 组函数, 0: 使用 B 组函数
    volatile int *ready;
    volatile int *start;
    uint64_t result;
    double elapsed_time;
} thread_arg_t;

static void *worker_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;

    bind_to_cpu(targ->cpu_id);
    print_cpu_bindind(targ->use_group_a ? "Thread-A" : "Thread-B");

    __atomic_fetch_add(targ->ready, 1, __ATOMIC_SEQ_CST);
    while (*targ->start == 0) {
        __asm__ __volatile__("pause" ::: "memory");
    }

    double start = get_time_sec();
    targ->result = targ->use_group_a ? run_func_group_a() : run_func_group_b();
    targ->elapsed_time = get_time_sec() - start;

    return NULL;
}

static void run_single_thread(void) {
    printf("\n=== Single Thread Test ===\n");

    bind_to_cpu(0);
    print_cpu_bindind("SingleThread");

    double start = get_time_sec();
    uint64_t result = run_func_group_a();
    double elapsed = get_time_sec() - start;

    printf("Result: %lu\n", result);
    printf("Time: %.4f seconds\n", elapsed);
}

static void run_dual_thread(int cpu1, int cpu2, const char *desc) {
    printf("\n=== %s ===\n", desc);
    printf("CPU binding: Thread-A -> CPU%d, Thread-B -> CPU%d\n", cpu1, cpu2);

    pthread_t threads[2];
    thread_arg_t args[2];
    volatile int ready = 0;
    volatile int start = 0;

    args[0] = (thread_arg_t){
        .cpu_id = cpu1, .use_group_a = 1,
        .ready = &ready, .start = &start
    };
    args[1] = (thread_arg_t){
        .cpu_id = cpu2, .use_group_a = 0,
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

    printf("Thread-A: Result=%lu, Time=%.4f sec\n", args[0].result, args[0].elapsed_time);
    printf("Thread-B: Result=%lu, Time=%.4f sec\n", args[1].result, args[1].elapsed_time);
    printf("Wall time: %.4f seconds\n", wall_elapsed);
}

int main(int argc, char *argv[]) {
    printf("=== I-Cache Contention Test ===\n");
    printf("Functions per group: 100\n");
    printf("Iterations: %d\n", ITERATIONS);
    printf("L1 I-Cache: 32 KB (shared by HT siblings)\n");

    const char *mode = argc > 1 ? argv[1] : "--all";

    if (strcmp(mode, "--same-core") == 0) {
        run_dual_thread(0, 8, "Same Core HT (CPU 0,8) - I-Cache Contention");
    } else if (strcmp(mode, "--diff-core") == 0) {
        run_dual_thread(0, 1, "Different Cores (CPU 0,1) - Independent I-Caches");
    } else if (strcmp(mode, "--single") == 0) {
        run_single_thread();
    } else if (strcmp(mode, "--all") == 0) {
        run_single_thread();
        run_dual_thread(0, 8, "Same Core HT (CPU 0,8) - I-Cache Contention");
        run_dual_thread(0, 1, "Different Cores (CPU 0,1) - Independent I-Caches");

        printf("\n=== Analysis ===\n");
        printf("Expected: Same-core HT with different code paths should be SLOWER\n");
        printf("         due to L1 I-cache contention (thrashing)\n");
    } else {
        printf("Usage: %s [--same-core | --diff-core | --single | --all]\n", argv[0]);
        return 1;
    }

    return 0;
}
