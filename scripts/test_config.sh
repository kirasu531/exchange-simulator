#!/usr/bin/env bash
set -e

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <dev|release|profile|asan|ubsan|tsan>"
    exit 1
fi

TYPE="$1"

case "$TYPE" in
    dev)
        DIR="build"
        ctest --test-dir "$DIR" --output-on-failure
        ;;

    release)
        DIR="build-release"
        ctest --test-dir "$DIR" --output-on-failure
        ;;

    profile)
        DIR="build-profile"
        ctest --test-dir "$DIR" --output-on-failure
        ;;

    asan)
        DIR="build-asan"

        setarch "$(uname -m)" -R \
            env ASAN_OPTIONS=halt_on_error=1 \
            ctest --test-dir "$DIR" --output-on-failure
        ;;

    ubsan)
        DIR="build-ubsan"

        env UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
            ctest --test-dir "$DIR" --output-on-failure
        ;;

    tsan)
        DIR="build-tsan"

        setarch "$(uname -m)" -R \
            env TSAN_OPTIONS=halt_on_error=1 \
            ctest --test-dir "$DIR" --output-on-failure
        ;;

    *)
        echo "Unknown build type: $TYPE"
        exit 1
        ;;
esac

echo "Tests passed for $TYPE"
