#!/usr/bin/env bash
set -e

JOBS=4

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <dev|release|profile|asan|ubsan|tsan>"
	exit 1
fi

TYPE="$1"

case "$TYPE" in
    dev)
        DIR="build"

        cmake -S . -B "$DIR" \
            -DCMAKE_BUILD_TYPE=Debug

        cmake --build "$DIR" -j "$JOBS"
        ;;

    release)
        DIR="build-release"

        cmake -S . -B "$DIR" \
            -DCMAKE_BUILD_TYPE=Release

        cmake --build "$DIR" -j "$JOBS"
        ;;

    profile)
        DIR="build-profile"

        cmake -S . -B "$DIR" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_FLAGS="-pg" \
            -DCMAKE_EXE_LINKER_FLAGS="-pg"

        cmake --build "$DIR" -j "$JOBS"
        ;;

    asan)
        DIR="build-asan"

        cmake -S . -B "$DIR" \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"

        setarch "$(uname -m)" -R cmake --build "$DIR" -j "$JOBS"
        ;;

    ubsan)
        DIR="build-ubsan"

        cmake -S . -B "$DIR" \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined"

        cmake --build "$DIR" -j "$JOBS"
        ;;

    tsan)
        DIR="build-tsan"

        cmake -S . -B "$DIR" \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"

        setarch "$(uname -m)" -R cmake --build "$DIR" -j "$JOBS"
        ;;

    *)
        echo "Unknown build type: $TYPE"
        exit 1
        ;;
esac

echo "Built $TYPE in $DIR"
