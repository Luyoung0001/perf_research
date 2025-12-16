# 超线程与缓存性能研究

本项目通过 perf 工具研究超线程（Hyper-Threading）和预取指令对 CPU 缓存性能的影响。

## 系统信息

- **CPU**: AMD Ryzen 7 8845HS (8 核心 16 线程)
- **超线程配对**: (0,8), (1,9), (2,10), (3,11), (4,12), (5,13), (6,14), (7,15)
- **L1 D-Cache**: 32KB（同核心超线程共享）
- **L1 I-Cache**: 32KB（同核心超线程共享）
- **L2 Cache**: 1MB（同核心超线程共享）
- **L3 Cache**: 16MB（所有核心共享）

## 快速开始

```bash
# 编译所有测试程序
./scripts/run_all_tests.sh --compile

# 运行快速测试
./scripts/run_all_tests.sh --quick

# 运行完整测试（需要 sudo 权限使用 perf）
sudo ./scripts/run_all_tests.sh --full
```

## 测试程序说明

### 负面场景（超线程缓存竞争）

| 程序 | 说明 |
|------|------|
| `src/negative/dcache_contention` | D-Cache 竞争：两线程访问不同内存区域 |
| `src/negative/icache_contention` | I-Cache 竞争：两线程执行不同代码路径 |
| `src/negative/false_sharing` | 伪共享问题演示 |

```bash
# 运行示例
./src/negative/dcache_contention --all
./src/negative/false_sharing --all
```

### 正面场景（超线程协作加速）

| 程序 | 说明 |
|------|------|
| `src/positive/shared_cache` | 共享缓存协作：两线程访问相同内存区域 |
| `src/positive/latency_hiding` | 延迟隐藏：计算+内存密集型混合 |

```bash
./src/positive/shared_cache --all
./src/positive/latency_hiding --all
```

### 预取指令测试

| 程序 | 说明 |
|------|------|
| `src/prefetch/sequential_prefetch` | 顺序访问预取效果 |
| `src/prefetch/random_prefetch` | 随机访问预取效果（软件预取最有效的场景） |
| `src/prefetch/matrix_prefetch` | 矩阵乘法预取优化 |
| `src/prefetch/prefetch_distance` | 预取距离对比 |
| `src/prefetch/prefetch_hints` | 预取提示类型对比（T0/T1/T2/NTA） |
| `src/prefetch/combined_test` | 超线程 + 预取综合测试 |

```bash
./src/prefetch/random_prefetch --all
./src/prefetch/prefetch_distance
./src/prefetch/combined_test
```

## 使用 perf 测量缓存性能

```bash
# 需要 root 权限或调整 perf_event_paranoid
# 临时调整（重启后失效）
sudo sysctl kernel.perf_event_paranoid=-1

# 或永久调整
echo 'kernel.perf_event_paranoid=-1' | sudo tee -a /etc/sysctl.conf

# 基础缓存测量
perf stat -e cache-misses,cache-references,L1-dcache-loads,L1-dcache-load-misses ./program

# 详细测量
perf stat -e cycles,instructions,cache-misses,cache-references,L1-dcache-loads,L1-dcache-load-misses,L1-icache-load-misses ./program
```

## 核心发现

### 1. 超线程负面场景
- 当两个超线程访问**不同**的大内存区域时，会争抢 L1 cache
- 结果：cache miss 率上升，性能下降
- **建议**：内存密集型任务绑定到不同核心

### 2. 超线程正面场景
- 当两个超线程访问**相同/相近**的内存区域时，可以共享缓存
- 延迟隐藏：内存等待时另一个线程可使用 CPU
- **建议**：数据共享型任务可利用超线程

### 3. False Sharing
- 不同线程修改同一缓存行的不同变量会导致严重性能下降
- 解决方案：使用 `__attribute__((aligned(64)))` 或填充到缓存行边界

### 4. 预取指令
- **顺序访问**：硬件预取器通常足够，软件预取收益有限
- **随机访问**：软件预取效果显著，是最佳应用场景
- **预取距离**：8-32 通常是最佳范围
- **预取提示**：
  - T0：适合即将重用的数据
  - NTA：适合流式处理（只用一次）

## CPU 绑定示例代码

```c
#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>

// 绑定当前线程到指定 CPU
int bind_to_cpu(int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    return sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
}

// 绑定到同一核心的超线程：CPU 0 和 8
bind_to_cpu(0);  // 线程 1
bind_to_cpu(8);  // 线程 2

// 绑定到不同核心：CPU 0 和 1
bind_to_cpu(0);  // 线程 1
bind_to_cpu(1);  // 线程 2
```

## 预取指令示例代码

```c
#include <xmmintrin.h>

// GCC 内置预取
__builtin_prefetch(addr, 0, 3);  // 读预取到 L1

// Intel intrinsics
_mm_prefetch((char*)addr, _MM_HINT_T0);   // 预取到 L1
_mm_prefetch((char*)addr, _MM_HINT_NTA);  // 非临时访问

// 典型使用模式
#define PREFETCH_DISTANCE 16
for (int i = 0; i < n; i++) {
    __builtin_prefetch(&arr[i + PREFETCH_DISTANCE], 0, 3);
    process(arr[i]);
}
```

## 文件结构

```
perf_research/
├── guide.md                    # 原始需求
├── plan.md                     # 详细计划
├── README.md                   # 本文件
├── src/
│   ├── common/
│   │   ├── cpu_bindind.h       # CPU 亲和性工具
│   │   └── prefetch_utils.h    # 预取指令封装
│   ├── negative/
│   │   ├── dcache_contention.c
│   │   ├── icache_contention.c
│   │   └── false_sharing.c
│   ├── positive/
│   │   ├── shared_cache.c
│   │   └── latency_hiding.c
│   └── prefetch/
│       ├── sequential_prefetch.c
│       ├── random_prefetch.c
│       ├── matrix_prefetch.c
│       ├── prefetch_distance.c
│       ├── prefetch_hints.c
│       └── combined_test.c
├── scripts/
│   ├── run_all_tests.sh
│   └── run_perf.sh
└── results/
    └── raw/
```
