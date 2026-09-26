#!/usr/bin/env bash
set -e
export LC_ALL=C

PORT=4000
RUNS=5

WORKLOADS=(
    Resting
    Random
    MostlyCrossing
    CancelHeavy
    ManyInstruments
    LargeSweep
)

SIZES=(
    10000
    100000
    500000
)

server_pid=""

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

    ./build-release/exchange_server "$PORT" > /dev/null 2>&1 &
    server_pid=$!

    sleep 0.1

    result=$(./build-release/exchange_network "$workload" "$operations")

    cleanup

    echo "$result"
}

run_test() {
    workload="$1"
    operations="$2"

    times=()
    timed_operations=""
    trades=""

    for ((run = 1; run <= RUNS; run++)); do
        result=$(run_once "$workload" "$operations")

        time=$(echo "$result" | awk '{gsub(/\(s\)/, "", $3); print $3}')
        timed_operations=$(echo "$result" | awk '{print $2}')
        trades=$(echo "$result" | awk '{print $5}')

        times+=("$time")
    done

    median_time=$(median "${times[@]}")

    printf "%s %s %.3f(s) %d %s\n" \
        "$workload" \
        "$timed_operations" \
        "$median_time" \
        "$RUNS" \
        "$trades"
}

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
