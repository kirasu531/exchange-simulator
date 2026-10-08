#!/usr/bin/env bash
set -e
export LC_ALL=C

PORT=4000
RUNS=5

WORKLOADS=(
    Resting
    Random
)

SIZES=(
    1000
)

cmake --build build-release -j > /dev/null

server_pid=""
temp_dir=$(mktemp -d)
log_path="$temp_dir/exchange.log"

cleanup() {
    if [ -n "$server_pid" ]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
        server_pid=""
    fi
}

trap cleanup EXIT

median() {
    printf '%s\n' "$@" |
        sort -n |
        awk '
        { values[NR] = $1 }
        END {
            if (NR % 2 == 1)
                print values[(NR + 1) / 2];
            else
                print (values[NR / 2] + values[NR / 2 + 1]) / 2;
        }'
}

run_once() {
    workload="$1"
    operations="$2"

    > "$log_path"
    ./build-release/exchange_server "--port" "$PORT" "--log" "$log_path" > /dev/null &
    server_pid=$!

    sleep 0.1

    run_result=$(./build-release/exchange_durability "$workload" "$operations")

    cleanup
}

run_test() {
    workload="$1"
    operations="$2"

    times=()
    throughputs=()
    avg_latencies=()
    p50s=()
    p95s=()
    p99s=()

    timed_operations=""
    trades=""

    for ((run = 1; run <= RUNS; run++)); do
        run_once "$workload" "$operations"
        result="$run_result"

        read -r type timed_operations trades time throughput avg_latency p50 p95 p99 <<< "$result"

        times+=("$time")
        throughputs+=("$throughput")
        avg_latencies+=("$avg_latency")
        p50s+=("$p50")
        p95s+=("$p95")
        p99s+=("$p99")
    done

    median_time=$(median "${times[@]}")
    median_throughput=$(median "${throughputs[@]}")
    median_avg_latency=$(median "${avg_latencies[@]}")
    median_p50=$(median "${p50s[@]}")
    median_p95=$(median "${p95s[@]}")
    median_p99=$(median "${p99s[@]}")

    printf "%s %s %s %.3f(s) %.0f %.3f(ms) %.3f(ms) %.3f(ms) %.3f(ms) %d\n" \
        "$workload" \
        "$timed_operations" \
        "$trades" \
        "$median_time" \
        "$median_throughput" \
        "$median_avg_latency" \
        "$median_p50" \
        "$median_p95" \
        "$median_p99" \
        "$RUNS"
}

echo "type operations trades time(s) throughput avgLatency(ms) p50(ms) p95(ms) p99(ms) runs"
echo "_____________________________________________________________________________________"

if [ "$#" -eq 0 ]; then
    for workload in "${WORKLOADS[@]}"; do
        for operations in "${SIZES[@]}"; do
            run_test "$workload" "$operations"
        done
    done

elif [ "$#" -eq 1 ]; then
    workload="$1"

    for operations in "${SIZES[@]}"; do
        run_test "$workload" "$operations"
    done

elif [ "$#" -eq 2 ]; then
    run_test "$1" "$2"

else
    echo "Usage:"
    echo "  $0"
    echo "  $0 <workload>"
    echo "  $0 <workload> <operations>"
    exit 1
fi

rm -rf "$temp_dir"