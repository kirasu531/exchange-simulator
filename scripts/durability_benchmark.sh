#!/usr/bin/env bash
set -e
export LC_ALL=C

PORT=4000
RUNS=5

WORKLOADS=(
    Resting
    Random
)

INTERVALS=(
    1
    2
    5
    10
    20
)

SIZES=(
    1000
    5000
    10000
    20000
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
    interval="$3"

    > "$log_path"
    ./build-release/exchange_server "--port" "$PORT" "--log" "$log_path" "--sync-interval-ms" "$interval" > /dev/null &
    server_pid=$!

    sleep 0.1

    run_result=$(./build-release/exchange_durability "$workload" "$operations" "$interval")

    cleanup
}

run_test() {
    workload="$1"
    operations="$2"
    interval="$3"

    times=()
    throughputs=()
    avg_latencies=()
    p50s=()
    p95s=()
    p99s=()
    sync_counts=()
    max_unsynceds=()

    timed_operations=""
    trades=""

    for ((run = 1; run <= RUNS; run++)); do
        run_once "$workload" "$operations" "$interval"
        result="$run_result"

        read -r type timed_operations trades time throughput avg_latency p50 p95 p99 sync_count max_unsynced <<< "$result"

        times+=("$time")
        throughputs+=("$throughput")
        avg_latencies+=("$avg_latency")
        p50s+=("$p50")
        p95s+=("$p95")
        p99s+=("$p99")
        sync_counts+=("$sync_count")
        max_unsynceds+=("$max_unsynced")
    done

    median_time=$(median "${times[@]}")
    median_throughput=$(median "${throughputs[@]}")
    median_avg_latency=$(median "${avg_latencies[@]}")
    median_p50=$(median "${p50s[@]}")
    median_p95=$(median "${p95s[@]}")
    median_p99=$(median "${p99s[@]}")
    median_sync_count=$(median "${sync_counts[@]}")
    median_max_unsynced=$(median "${max_unsynced[@]}")

    printf "%s %s %s %.3f(s) %.0f %.3f(ms) %.3f(ms) %.3f(ms) %.3f(ms) %d %d %d(ms) %d\n" \
        "$workload" \
        "$timed_operations" \
        "$trades" \
        "$median_time" \
        "$median_throughput" \
        "$median_avg_latency" \
        "$median_p50" \
        "$median_p95" \
        "$median_p99" \
        "$median_sync_count" \
        "$median_max_unsynced" \
        "$interval" \
        "$RUNS"
}

echo "type operations trades time(s) throughput avgLatency(ms) p50(ms) p95(ms) p99(ms) sync_count max_unsynced interval_time runs"
echo "___________________________________________________________________________________________________________________________"

if [ "$#" -eq 0 ]; then
    for workload in "${WORKLOADS[@]}"; do
        for operations in "${SIZES[@]}"; do
            for interval in "${INTERVALS[@]}"; do
                run_test "$workload" "$operations" "$interval"
            done
        done
    done

elif [ "$#" -eq 1 ]; then
    workload="$1"

    for operations in "${SIZES[@]}"; do
        for interval in "${INTERVALS[@]}"; do
            run_test "$workload" "$operations" "$interval"
        done
    done

elif [ "$#" -eq 2 ]; then
    workload="$1"
    operations="$2"

    for interval in "${INTERVALS[@]}"; do
        run_test "$workload" "$operations" "$interval"
    done

elif [ "$#" -eq 3 ]; then
    run_test "$1" "$2" "$3"

else
    echo "Usage:"
    echo "  $0"
    echo "  $0 <workload>"
    echo "  $0 <workload> <operations>"
    exit 1
fi

rm -rf "$temp_dir"