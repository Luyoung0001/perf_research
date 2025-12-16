/*
 * matrix_prefetch.c - 矩阵运算预取测试
 *
 * 对比矩阵乘法中使用预取指令的效果。
 *
 * 编译: gcc -O2 -o matrix_prefetch matrix_prefetch.c
 * 运行: ./matrix_prefetch [--naive | --prefetch | --blocked | --all]
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../common/cpu_bindind.h"
#include "../common/prefetch_utils.h"

// 矩阵大小
#define N 1024
#define BLOCK_SIZE 64  // 分块大小

static double *A, *B, *C;

// 分配矩阵
static double *alloc_matrix(void) {
    return aligned_alloc(CACHE_LINE_SIZE, N * N * sizeof(double));
}

// 初始化矩阵
static void init_matrix(double *M, double val) {
    for (int i = 0; i < N * N; i++) {
        M[i] = val + (i % 100) * 0.01;
    }
}

// 清零矩阵
static void zero_matrix(double *M) {
    memset(M, 0, N * N * sizeof(double));
}

// 朴素矩阵乘法 - 无优化
static void matmul_naive(void) {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            double sum = 0;
            for (int k = 0; k < N; k++) {
                sum += A[i * N + k] * B[k * N + j];
            }
            C[i * N + j] = sum;
        }
    }
}

// 带预取的矩阵乘法
static void matmul_prefetch(void) {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            double sum = 0;

            // 预取 A 的下一行
            if (j == 0 && i + 1 < N) {
                for (int p = 0; p < N; p += 8) {
                    PREFETCH_T0(&A[(i + 1) * N + p]);
                }
            }

            for (int k = 0; k < N; k++) {
                // 预取 B 的下几行
                if (k + 8 < N) {
                    PREFETCH_T0(&B[(k + 8) * N + j]);
                }
                sum += A[i * N + k] * B[k * N + j];
            }
            C[i * N + j] = sum;
        }
    }
}

// 分块矩阵乘法 - 更好的缓存局部性
static void matmul_blocked(void) {
    for (int ii = 0; ii < N; ii += BLOCK_SIZE) {
        for (int jj = 0; jj < N; jj += BLOCK_SIZE) {
            for (int kk = 0; kk < N; kk += BLOCK_SIZE) {
                // 处理一个块
                for (int i = ii; i < ii + BLOCK_SIZE && i < N; i++) {
                    for (int j = jj; j < jj + BLOCK_SIZE && j < N; j++) {
                        double sum = C[i * N + j];
                        for (int k = kk; k < kk + BLOCK_SIZE && k < N; k++) {
                            sum += A[i * N + k] * B[k * N + j];
                        }
                        C[i * N + j] = sum;
                    }
                }
            }
        }
    }
}

// 分块 + 预取
static void matmul_blocked_prefetch(void) {
    for (int ii = 0; ii < N; ii += BLOCK_SIZE) {
        for (int jj = 0; jj < N; jj += BLOCK_SIZE) {
            // 预取下一个 B 块
            if (jj + BLOCK_SIZE < N) {
                for (int p = 0; p < BLOCK_SIZE && p < N; p++) {
                    PREFETCH_T0(&B[p * N + jj + BLOCK_SIZE]);
                }
            }

            for (int kk = 0; kk < N; kk += BLOCK_SIZE) {
                // 预取下一个 A 块
                if (kk + BLOCK_SIZE < N) {
                    for (int p = ii; p < ii + BLOCK_SIZE && p < N; p++) {
                        PREFETCH_T0(&A[p * N + kk + BLOCK_SIZE]);
                    }
                }

                for (int i = ii; i < ii + BLOCK_SIZE && i < N; i++) {
                    for (int j = jj; j < jj + BLOCK_SIZE && j < N; j++) {
                        double sum = C[i * N + j];
                        for (int k = kk; k < kk + BLOCK_SIZE && k < N; k++) {
                            sum += A[i * N + k] * B[k * N + j];
                        }
                        C[i * N + j] = sum;
                    }
                }
            }
        }
    }
}

static void run_test(const char *name, void (*test_func)(void)) {
    printf("\n=== %s ===\n", name);

    zero_matrix(C);

    double start = get_time_sec();
    test_func();
    double elapsed = get_time_sec() - start;

    // 计算 GFLOPS (2 * N^3 浮点运算)
    double gflops = 2.0 * N * N * N / elapsed / 1e9;

    // 验证结果（检查一个元素）
    printf("C[0][0] = %.6f\n", C[0]);
    printf("Time: %.4f seconds\n", elapsed);
    printf("Performance: %.2f GFLOPS\n", gflops);
}

int main(int argc, char *argv[]) {
    A = alloc_matrix();
    B = alloc_matrix();
    C = alloc_matrix();

    if (!A || !B || !C) {
        perror("Memory allocation failed");
        return 1;
    }

    init_matrix(A, 1.0);
    init_matrix(B, 2.0);

    bind_to_cpu(0);

    printf("=== Matrix Multiplication Prefetch Test ===\n");
    printf("Matrix size: %d x %d\n", N, N);
    printf("Block size: %d\n", BLOCK_SIZE);
    printf("Total operations: %.2f GFLOP\n", 2.0 * N * N * N / 1e9);

    const char *mode = argc > 1 ? argv[1] : "--all";

    if (strcmp(mode, "--naive") == 0) {
        run_test("Naive (ijk order)", matmul_naive);
    } else if (strcmp(mode, "--prefetch") == 0) {
        run_test("With Prefetch", matmul_prefetch);
    } else if (strcmp(mode, "--blocked") == 0) {
        run_test("Blocked (cache-friendly)", matmul_blocked);
    } else if (strcmp(mode, "--blocked-prefetch") == 0) {
        run_test("Blocked + Prefetch", matmul_blocked_prefetch);
    } else if (strcmp(mode, "--all") == 0) {
        run_test("Naive (ijk order)", matmul_naive);
        run_test("Naive + Prefetch", matmul_prefetch);
        run_test("Blocked (cache-friendly)", matmul_blocked);
        run_test("Blocked + Prefetch", matmul_blocked_prefetch);

        printf("\n=== Analysis ===\n");
        printf("1. Naive: Poor cache locality, many cache misses\n");
        printf("2. Prefetch: Helps with naive but limited benefit\n");
        printf("3. Blocked: Much better cache locality\n");
        printf("4. Blocked+Prefetch: Best of both worlds\n");
        printf("\nKey insight: Algorithm optimization (blocking) often\n");
        printf("matters more than prefetching, but combining both is best.\n");
    } else {
        printf("Usage: %s [--naive | --prefetch | --blocked | --blocked-prefetch | --all]\n", argv[0]);
        free(A); free(B); free(C);
        return 1;
    }

    free(A);
    free(B);
    free(C);
    return 0;
}
