#!/usr/bin/env bash
set -e
export LC_ALL=C

RUNS=5
OPERATIONS=1000

BATCHES=(
    1
    2
    4
    8
    16
    32
    64
    128
    256
    512
    1000
)

cmake --build build-release -j > /dev/null

temp_dir=$(mktemp -d)
log_path="$temp_dir/exchange.log"

cleanup() {
    rm -rf "$temp_dir"
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


echo "operations batch medianTime(s) records/s runs"
echo "---------------------------------------------"

for batch in "${BATCHES[@]}"; do
    times=()

    for ((run = 1; run <= RUNS; run++)); do
        > "$log_path"

        result=$(
            ./build-release/logsync_bench \
                "$log_path" \
                "$OPERATIONS" \
                "$batch"
        )

        read -r operations reported_batch time <<< "$result"

        times+=("$time")
    done

    median_time=$(median "${times[@]}")

    throughput=$(
        awk \
            -v operations="$OPERATIONS" \
            -v time="$median_time" \
            'BEGIN { print operations / time }'
    )

    printf "%-10d %-6d %-13.6f %-10.0f %d\n" \
        "$OPERATIONS" \
        "$batch" \
        "$median_time" \
        "$throughput" \
        "$RUNS"
done