#!/usr/bin/env bash
set -e

if [ "$#" -ne 2 ] && [ "$#" -ne 3 ]; then
	echo "Usage: ./profile <workload> <operationCount> [checkpoint]"
	exit 1
fi

rm -f gmon.out
./build-profile/exchange_bench "$1" "$2"

workload="$1"
operations="$2"

#mv "profiles/${workload}_2.txt" "profiles/${workload}_3.txt" 2>/dev/null
mv "profiles/${workload}_1.txt" "profiles/${workload}_2.txt" 2>/dev/null

gprof -b -p ./build-profile/exchange_bench gmon.out \
	| python3 tools/summarize_profile.py "$workload" \
	> "profiles/${workload}_1.txt"
	
rm -f gmon.out

if [ "$#" -eq 3 ]; then
	cp "profiles/${workload}_1.txt" "profiles/$3_${workload}_${operations}.txt"
fi

gedit "profiles/${workload}_1.txt"
