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

        cmake -S . -B "$DIR" \
            -DCMAKE_BUILD_TYPE=Debug

        cmake --build "$DIR" -j
        ;;

    release)
        DIR="build-release"

        cmake -S . -B "$DIR" \
            -DCMAKE_BUILD_TYPE=Release

        cmake --build "$DIR" -j
        ;;

    profile)
        DIR="build-profile"

        cmake -S . -B "$DIR" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_FLAGS="-pg" \
            -DCMAKE_EXE_LINKER_FLAGS="-pg"

        cmake --build "$DIR" -j
        ;;

    asan)
        DIR="build-asan"

        cmake -S . -B "$DIR" \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"

        setarch "$(uname -m)" -R cmake --build "$DIR" -j
        ;;

    ubsan)
        DIR="build-ubsan"

        cmake -S . -B "$DIR" \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined"

        cmake --build "$DIR" -j
        ;;

    tsan)
        DIR="build-tsan"

        cmake -S . -B "$DIR" \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"

        setarch "$(uname -m)" -R cmake --build "$DIR" -j
        ;;

    *)
        echo "Unknown build type: $TYPE"
        exit 1
        ;;
esac

echo "Built $TYPE in $DIR"
