#!/bin/bash

# run_all_tests.sh - 运行所有测试并收集结果
# 使用方法: ./run_all_tests.sh [--quick | --full]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SRC_DIR="$PROJECT_DIR/src"
RESULT_DIR="$PROJECT_DIR/results"
RAW_DIR="$RESULT_DIR/raw"

# 创建结果目录
mkdir -p "$RAW_DIR"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 编译所有测试程序
compile_all() {
    log_info "Compiling all test programs..."

    cd "$SRC_DIR"

    # 负面场景
    log_info "Compiling negative tests..."
    gcc -O2 -pthread -o negative/dcache_contention negative/dcache_contention.c
    gcc -O2 -pthread -o negative/icache_contention negative/icache_contention.c
    gcc -O2 -pthread -o negative/false_sharing negative/false_sharing.c

    # 正面场景
    log_info "Compiling positive tests..."
    gcc -O2 -pthread -o positive/shared_cache positive/shared_cache.c
    gcc -O2 -pthread -o positive/latency_hiding positive/latency_hiding.c -lm

    # 预取测试
    log_info "Compiling prefetch tests..."
    gcc -O2 -o prefetch/sequential_prefetch prefetch/sequential_prefetch.c
    gcc -O2 -o prefetch/random_prefetch prefetch/random_prefetch.c
    gcc -O2 -o prefetch/matrix_prefetch prefetch/matrix_prefetch.c
    gcc -O2 -o prefetch/prefetch_distance prefetch/prefetch_distance.c
    gcc -O2 -o prefetch/prefetch_hints prefetch/prefetch_hints.c
    gcc -O2 -pthread -o prefetch/combined_test prefetch/combined_test.c

    log_success "All programs compiled successfully!"
}

# 运行测试并使用 perf 收集数据
run_with_perf() {
    local program="$1"
    local name="$2"
    local args="${@:3}"
    local output_file="$RAW_DIR/${name}.txt"

    log_info "Running: $name"

    echo "=== $name ===" > "$output_file"
    echo "Command: $program $args" >> "$output_file"
    echo "Date: $(date)" >> "$output_file"
    echo "" >> "$output_file"

    # 运行程序本身的输出
    echo "--- Program Output ---" >> "$output_file"
    "$program" $args >> "$output_file" 2>&1

    echo "" >> "$output_file"
    echo "--- Perf Stats ---" >> "$output_file"

    # 使用 perf 收集缓存统计
    perf stat -e cache-misses,cache-references,L1-dcache-loads,L1-dcache-load-misses,L1-icache-load-misses,cycles,instructions \
        "$program" $args >> "$output_file" 2>&1

    log_success "Results saved to $output_file"
}

# 运行负面场景测试
run_negative_tests() {
    log_info "=== Running Negative Scenario Tests ==="

    cd "$SRC_DIR"

    # D-Cache 竞争测试
    run_with_perf ./negative/dcache_contention "dcache_single" --single
    run_with_perf ./negative/dcache_contention "dcache_same_core" --same-core
    run_with_perf ./negative/dcache_contention "dcache_diff_core" --diff-core

    # I-Cache 竞争测试
    run_with_perf ./negative/icache_contention "icache_single" --single
    run_with_perf ./negative/icache_contention "icache_same_core" --same-core
    run_with_perf ./negative/icache_contention "icache_diff_core" --diff-core
}

# 运行正面场景测试
run_positive_tests() {
    log_info "=== Running Positive Scenario Tests ==="

    cd "$SRC_DIR"

    # 共享缓存测试
    run_with_perf ./positive/shared_cache "shared_single" --single
    run_with_perf ./positive/shared_cache "shared_same_core" --same-core
    run_with_perf ./positive/shared_cache "shared_diff_core" --diff-core

    # 延迟隐藏测试
    run_with_perf ./positive/latency_hiding "latency_single" --single
    run_with_perf ./positive/latency_hiding "latency_same_core" --same-core
    run_with_perf ./positive/latency_hiding "latency_diff_core" --diff-core
}

# 运行预取测试
run_prefetch_tests() {
    log_info "=== Running Prefetch Tests ==="

    cd "$SRC_DIR"

    # 顺序访问预取
    run_with_perf ./prefetch/sequential_prefetch "seq_no_prefetch" --no-prefetch
    run_with_perf ./prefetch/sequential_prefetch "seq_prefetch" --prefetch
    run_with_perf ./prefetch/sequential_prefetch "seq_prefetch_nta" --prefetch-nta

    # 随机访问预取
    run_with_perf ./prefetch/random_prefetch "rand_no_prefetch" --no-prefetch
    run_with_perf ./prefetch/random_prefetch "rand_prefetch" --prefetch
    run_with_perf ./prefetch/random_prefetch "rand_multi_prefetch" --multi-prefetch

    # 矩阵预取
    run_with_perf ./prefetch/matrix_prefetch "matrix_naive" --naive
    run_with_perf ./prefetch/matrix_prefetch "matrix_prefetch" --prefetch
    run_with_perf ./prefetch/matrix_prefetch "matrix_blocked" --blocked
    run_with_perf ./prefetch/matrix_prefetch "matrix_blocked_prefetch" --blocked-prefetch

    # 预取距离和提示类型
    run_with_perf ./prefetch/prefetch_distance "prefetch_distance" ""
    run_with_perf ./prefetch/prefetch_hints "prefetch_hints" ""
}

# 生成摘要报告
generate_summary() {
    log_info "Generating summary report..."

    local summary_file="$RESULT_DIR/summary.txt"

    echo "=== Performance Test Summary ===" > "$summary_file"
    echo "Generated: $(date)" >> "$summary_file"
    echo "" >> "$summary_file"

    echo "=== System Info ===" >> "$summary_file"
    lscpu | grep -E "(Model name|CPU\(s\)|Thread|Core|L1|L2|L3)" >> "$summary_file"
    echo "" >> "$summary_file"

    echo "=== Test Results ===" >> "$summary_file"
    for f in "$RAW_DIR"/*.txt; do
        if [ -f "$f" ]; then
            echo "--- $(basename "$f" .txt) ---" >> "$summary_file"
            grep -E "(Time:|Wall time:|Throughput:|Bandwidth:|Performance:|cache-misses|L1-dcache)" "$f" 2>/dev/null | head -10 >> "$summary_file"
            echo "" >> "$summary_file"
        fi
    done

    log_success "Summary saved to $summary_file"
}

# 快速测试（只运行主要测试）
run_quick() {
    compile_all

    log_info "Running quick tests..."

    cd "$SRC_DIR"

    # 运行综合测试
    log_info "D-Cache contention test..."
    ./negative/dcache_contention --all 2>&1 | tee "$RAW_DIR/dcache_all.txt"

    log_info "False sharing test..."
    ./negative/false_sharing --all 2>&1 | tee "$RAW_DIR/false_sharing_all.txt"

    log_info "Shared cache test..."
    ./positive/shared_cache --all 2>&1 | tee "$RAW_DIR/shared_all.txt"

    log_info "Random prefetch test..."
    ./prefetch/random_prefetch --all 2>&1 | tee "$RAW_DIR/random_prefetch_all.txt"

    log_info "Combined HT + Prefetch test..."
    ./prefetch/combined_test 2>&1 | tee "$RAW_DIR/combined_test.txt"

    log_success "Quick tests completed!"
}

# 完整测试
run_full() {
    compile_all
    run_negative_tests
    run_positive_tests
    run_prefetch_tests
    generate_summary

    log_success "All tests completed! Results in $RESULT_DIR"
}

# 主函数
case "${1:-quick}" in
    --quick|-q)
        run_quick
        ;;
    --full|-f)
        run_full
        ;;
    --compile|-c)
        compile_all
        ;;
    --negative)
        compile_all
        run_negative_tests
        ;;
    --positive)
        compile_all
        run_positive_tests
        ;;
    --prefetch)
        compile_all
        run_prefetch_tests
        ;;
    --help|-h)
        echo "Usage: $0 [option]"
        echo ""
        echo "Options:"
        echo "  --quick, -q    Run quick tests (default)"
        echo "  --full, -f     Run all tests with perf"
        echo "  --compile, -c  Only compile programs"
        echo "  --negative     Run negative scenario tests"
        echo "  --positive     Run positive scenario tests"
        echo "  --prefetch     Run prefetch tests"
        echo "  --help, -h     Show this help"
        ;;
    *)
        run_quick
        ;;
esac
