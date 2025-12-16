#!/bin/bash

# perf 性能测量脚本
# 用于测量缓存命中率、IPC 等指标

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULT_DIR="${SCRIPT_DIR}/../results/raw"

# 确保结果目录存在
mkdir -p "$RESULT_DIR"

# perf 事件列表
CACHE_EVENTS="cache-misses,cache-references,L1-dcache-loads,L1-dcache-load-misses,L1-icache-load-misses"
BASIC_EVENTS="cycles,instructions"
AMD_EVENTS="l2_cache_req_stat.ic_dc_miss_in_l2,l2_cache_req_stat.ic_dc_hit_in_l2"

# 运行次数
RUNS=${RUNS:-5}

run_perf() {
    local program="$1"
    local name="$2"
    local extra_args="${@:3}"

    echo "=========================================="
    echo "Testing: $name"
    echo "Program: $program $extra_args"
    echo "Runs: $RUNS"
    echo "=========================================="

    # 运行 perf stat
    perf stat -r "$RUNS" \
        -e "$CACHE_EVENTS,$BASIC_EVENTS" \
        -- "$program" $extra_args 2>&1 | tee "${RESULT_DIR}/${name}.txt"

    echo ""
}

# 简单运行（不重复）
run_perf_once() {
    local program="$1"
    local name="$2"
    local extra_args="${@:3}"

    echo "=========================================="
    echo "Testing: $name"
    echo "=========================================="

    perf stat -e "$CACHE_EVENTS,$BASIC_EVENTS" \
        -- "$program" $extra_args 2>&1 | tee "${RESULT_DIR}/${name}.txt"

    echo ""
}

# 详细模式（包含更多事件）
run_perf_detailed() {
    local program="$1"
    local name="$2"
    local extra_args="${@:3}"

    echo "=========================================="
    echo "Detailed Testing: $name"
    echo "=========================================="

    perf stat -r "$RUNS" \
        -e "cycles,instructions,cache-misses,cache-references" \
        -e "L1-dcache-loads,L1-dcache-load-misses" \
        -e "L1-icache-load-misses" \
        -e "LLC-loads,LLC-load-misses" \
        -- "$program" $extra_args 2>&1 | tee "${RESULT_DIR}/${name}_detailed.txt"

    echo ""
}

# 对比两种配置
compare_configs() {
    local program="$1"
    local name1="$2"
    local args1="$3"
    local name2="$4"
    local args2="$5"

    echo "=========================================="
    echo "Comparison Test"
    echo "=========================================="

    echo ">>> Config 1: $name1"
    run_perf "$program" "$name1" $args1

    echo ">>> Config 2: $name2"
    run_perf "$program" "$name2" $args2

    echo "Results saved to: ${RESULT_DIR}/"
}

# 打印使用说明
usage() {
    echo "Usage: $0 <command> [options]"
    echo ""
    echo "Commands:"
    echo "  run <program> <name> [args]     - Run perf stat on program"
    echo "  detailed <program> <name> [args] - Run detailed perf stat"
    echo "  compare <program> <n1> <a1> <n2> <a2> - Compare two configs"
    echo ""
    echo "Environment:"
    echo "  RUNS=N  - Number of runs (default: 5)"
    echo ""
    echo "Examples:"
    echo "  $0 run ./test baseline"
    echo "  $0 run ./test ht_same --same-core"
    echo "  RUNS=10 $0 detailed ./test detailed_test"
}

# 主函数
case "$1" in
    run)
        shift
        run_perf "$@"
        ;;
    once)
        shift
        run_perf_once "$@"
        ;;
    detailed)
        shift
        run_perf_detailed "$@"
        ;;
    compare)
        shift
        compare_configs "$@"
        ;;
    *)
        usage
        ;;
esac
