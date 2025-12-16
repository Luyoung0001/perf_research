#ifndef PREFETCH_UTILS_H
#define PREFETCH_UTILS_H

#include <xmmintrin.h>

// 预取提示类型说明:
// T0  - 预取到所有级别缓存 (L1, L2, L3)
// T1  - 预取到 L2 及以上
// T2  - 预取到 L3 及以上
// NTA - 非临时访问，数据用完后不保留在缓存中

// GCC 内置预取封装
// rw: 0=读预取, 1=写预取
// locality: 0=NTA, 1=T2, 2=T1, 3=T0

// 预取到 L1 (最近)
#define PREFETCH_T0(addr) __builtin_prefetch((addr), 0, 3)

// 预取到 L2
#define PREFETCH_T1(addr) __builtin_prefetch((addr), 0, 2)

// 预取到 L3
#define PREFETCH_T2(addr) __builtin_prefetch((addr), 0, 1)

// 非临时访问预取
#define PREFETCH_NTA(addr) __builtin_prefetch((addr), 0, 0)

// 写预取 (预取并标记为即将写入)
#define PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 3)

// Intel Intrinsics 预取封装
#define PREFETCH_MM_T0(addr)  _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#define PREFETCH_MM_T1(addr)  _mm_prefetch((const char*)(addr), _MM_HINT_T1)
#define PREFETCH_MM_T2(addr)  _mm_prefetch((const char*)(addr), _MM_HINT_T2)
#define PREFETCH_MM_NTA(addr) _mm_prefetch((const char*)(addr), _MM_HINT_NTA)

// 缓存行大小 (AMD Ryzen: 64 bytes)
#define CACHE_LINE_SIZE 64

// 计算预取距离 (元素个数)
// prefetch_distance = (memory_latency * loop_throughput) / element_size
// 典型值: 8-64 个元素

// 预取下 N 个缓存行
#define PREFETCH_NEXT_LINES(addr, n) do { \
    for (int _i = 1; _i <= (n); _i++) { \
        PREFETCH_T0((char*)(addr) + _i * CACHE_LINE_SIZE); \
    } \
} while(0)

// 清除缓存行 (用于冷缓存测试)
#define CLFLUSH(addr) __asm__ __volatile__("clflush (%0)" :: "r"(addr) : "memory")

// 对齐到缓存行
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))

// 确保变量在不同缓存行，避免 false sharing
#define CACHE_PADDED(type, name) \
    struct { type value; char padding[CACHE_LINE_SIZE - sizeof(type)]; } name

#endif // PREFETCH_UTILS_H
