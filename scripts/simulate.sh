#!/usr/bin/env bash
set -e

server_pid=""

cleanup() {
	if [ -n "$server_pid" ]; then
		kill "$server_pid" 2> /dev/null || true
		wait "$server_pid" 2> /dev/null || true
		server_pid=""
	fi
}

trap cleanup EXIT

echo "____________________________________________"

if [[ "$#" -le 0 || ( "$1" != "new" && "$1" != "log" ) ]]; then
	echo "Usage: $0 <new|log>  <debug|empty>"
	exit 1
fi

if [[ "$1" == "new" ]]; then
	echo "--------- Starting new simulation ----------"
	echo "____________________________________________"
	
	> exchange.log
else
	echo "------------ Restoring the log -------------"
	echo "____________________________________________" 
fi

echo

cmake --build build -j > /dev/null
echo "Build successful"
echo

if [[ "$#" -eq 2 && "$2" == "debug"W ]]; then
	./build/exchange_server "4000" "--log" "exchange.log" &
else
	./build/exchange_server "4000" "--log" "exchange.log" > /dev/null & 
fi

server_pid=$!

sleep 0.1
 
./build/exchange_client
