# C++ Exchange Simulator

A multi-instrument exchange matching engine and TCP server written in C++20.

The project was built incrementally to learn practical C++ software engineering and systems programming: architecture, testing, benchmarking, profiling, networking, concurrency, sanitizers, and CI.

## Features

### Matching engine

- Limit and market orders
- Buy and sell sides
- Multiple instruments
- Price-time priority
- FIFO ordering within each price level
- Partial fills and multi-order matching
- Order cancellation
- Globally unique order IDs
- Deterministic arrival and trade sequencing
- Active-order lookup and trade history

### Network server

- Persistent TCP connections
- Newline-delimited text protocol
- Multiple simultaneous clients
- One thread per connected client
- Mutex-protected matching-engine access
- Partial-send handling
- TCP message framing across arbitrary `recv()` boundaries
- Input validation and error responses

## Architecture

```text
                 TCP clients
               /      |      \
              /       |       \
        client     client     client
        thread     thread     thread
              \       |       /
               \      |      /
                 engine mutex
                      |
                MatchingEngine
                      |
          +-----------+-----------+
          |           |           |
      OrderBook   OrderBook      ...
        BTC         AAPL
```

`MatchingEngine` owns global order/trade sequencing and routes orders to per-instrument `OrderBook`s.

Each order book stores price levels as:

```text
price -> FIFO queue of orders
```

Cancellation uses active-order tracking with lazy removal from price-level queues.

## TCP Protocol

Commands are UTF-8/ASCII text terminated by `\n`.

Examples:

```text
ADD 1 100 5 BUY LIMIT BTC
ADD 2 0 10 SELL MARKET BTC
GET 1
CANCEL 1
TRADES
```

The server reconstructs complete commands even when TCP splits or combines transmitted byte chunks.

## Project Structure

```text
include/        public types and interfaces
src/            matching engine, CLI, and TCP server
tests/          Catch2 and network integration tests
benchmarks/     direct and network benchmark code/results
profiles/       summarized profiling results
scripts/        build, benchmark, and profiling helpers
tools/          auxiliary tooling
.github/        GitHub Actions CI
```

## Build

Requirements:

- CMake 3.20+
- C++20 compiler
- Python 3 for network integration tests

```bash
cmake -S . -B build
cmake --build build -j
```

Run the original CLI:

```bash
./build/exchange
```

Run the TCP server:

```bash
./build/exchange_server 4000
```

## Testing

Run the complete CTest suite:

```bash
ctest --test-dir build --output-on-failure
```

The suite covers matching-engine behavior as well as end-to-end TCP communication across multiple clients.

Additional validation is performed with:

- AddressSanitizer
- UndefinedBehaviorSanitizer
- ThreadSanitizer

The network integration tests include concurrent-client stress testing.

## Benchmarking

Benchmarks use deterministic workload generation with seed `1337`.

Workloads:

- `Resting`
- `Random`
- `MostlyCrossing`
- `CancelHeavy`
- `ManyInstruments`
- `LargeSweep`

### Direct engine benchmark

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
./build-release/exchange_bench
```

### Network benchmark

The network benchmark uses the same workload definitions but sends operations through the TCP server and waits for each command response.

```bash
./scripts/network_benchmark.sh
```

It starts a fresh server for each run and reports the median of repeated runs.

## V0.1 -> V0.2 Performance

V0.2 used profiling to replace the original order-oriented tree representation with price-level FIFO queues and lazy cancellation.

500K-input results:

| Workload | V0.1 | V0.2 | Improvement |
|---|---:|---:|---:|
| Resting | 0.788 s | 0.163 s | ~79% |
| Random | 0.569 s | 0.377 s | ~34% |
| MostlyCrossing | 0.332 s | 0.161 s | ~52% |
| CancelHeavy | 1.946 s | 0.689 s | ~65% |
| ManyInstruments | 0.816 s | 0.662 s | ~19% |
| LargeSweep | 0.157 s | 0.068 s | ~57% |

Full results:

- [V0.1 baseline](benchmarks/v0.1_baseline.md)
- [V0.2 baseline](benchmarks/v0.2_baseline.md)
- [V0.1 profiling findings](benchmarks/profiling.md)
- [V0.3 network baseline](benchmarks/v0.3_network_baseline.md)

## Version Progression

```text
V0.1  Matching-engine correctness and automated testing
  ↓
V0.2  Profiling, performance redesign, sanitizers, and CI
  ↓
V0.3  TCP networking, concurrent clients, integration testing,
      ThreadSanitizer validation, and network benchmarking
```

## Tooling

- C++20
- CMake
- Catch2 / CTest
- POSIX sockets
- C++ threads and mutexes
- GNU gprof
- ASan / UBSan / TSan
- Python integration tests
- Bash automation
- Git / GitHub
- GitHub Actions

## Status

**V0.3**

The current version provides a tested multi-client TCP interface around the matching engine, with deterministic direct and network benchmarks and sanitizer-backed concurrency validation.
