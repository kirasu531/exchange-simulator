# C++ Exchange Simulator

A multi-instrument exchange matching engine written in C++20.

I built this project to move from competitive-programming-style C++ toward practical software engineering and systems programming: multi-file design, testing, benchmarking, profiling, performance-oriented data structures, sanitizers, and CI.

## Features

* Limit and market orders
* Buy and sell sides
* Multiple instruments
* Price-time priority
* FIFO ordering within a price level
* Partial fills
* One order matching multiple resting orders
* Order cancellation
* Globally unique order IDs
* Deterministic arrival sequencing
* Globally ordered trade sequencing
* Active-order lookup
* Trade history
* Order validation

## Architecture

```text
                 MatchingEngine
                      |
          +-----------+-----------+
          |           |           |
        BTC         AAPL         ...
          |           |
      OrderBook    OrderBook
```

`MatchingEngine` is the public entry point. It validates and routes orders, assigns arrival sequences, tracks order IDs, handles cancellation/lookups, and maintains global trade sequencing.

Each instrument has its own `OrderBook`.

### Order book representation

Price levels are stored as ordered maps:

```text
price -> FIFO queue of orders
```

This gives:

* highest bid / lowest ask price priority;
* FIFO priority inside a price level;
* efficient removal of completed price levels.

Cancellation uses active-order tracking plus lazy removal from the price-level queues.

Trades execute at the resting order's price.

Unfilled limit orders remain in the book. Unfilled market-order quantity is discarded.

## Project Structure

```text
include/        public types and interfaces
src/            matching-engine implementation and CLI
tests/          Catch2 test suite
benchmarks/     deterministic benchmark harness and results
profiles/       summarized profiling results
tools/          profiling helper scripts
.github/        GitHub Actions CI
```

The core engine is built as the `exchange_core` library and reused by the CLI, tests, and benchmark executable.

## Build

Requirements:

* CMake 3.20+
* C++20 compiler

Configure and build:

```bash
cmake -S . -B build
cmake --build build -j
```

Run the CLI:

```bash
./build/exchange
```

## Testing

The project uses Catch2 with CTest.

```bash
ctest --test-dir build --output-on-failure
```

The current test suite contains 22 test cases covering:

* validation and result codes;
* duplicate IDs;
* arrival sequencing;
* non-crossing orders;
* instrument isolation;
* partial and complete fills;
* best bid / best ask priority;
* FIFO ordering;
* multi-order matching;
* market-order behavior;
* cancellation;
* global trade sequencing and trade history.

The engine has also been checked locally with AddressSanitizer and UndefinedBehaviorSanitizer.

## Benchmarking

Benchmarks use a deterministic random seed:

```text
1337
```

Input generation happens outside the timed region, and repeated runs use fresh `MatchingEngine` instances.

Workloads:

* `Resting` — growing non-crossing book
* `Random` — randomized orders
* `MostlyCrossing` — frequent matches
* `CancelHeavy` — insertion followed by heavy cancellation
* `ManyInstruments` — large number of instruments
* `LargeSweep` — a few aggressive orders sweeping a large resting book

Create an optimized build:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
./build-release/exchange_bench
```

## V0.1 -> V0.2 Performance

The same deterministic workloads were measured before and after the V0.2 redesign.

500K-input results:

| Workload        |    V0.1 |    V0.2 |      Change |
| --------------- | ------: | ------: | ----------: |
| Resting         | 0.788 s | 0.163 s | ~79% faster |
| Random          | 0.569 s | 0.377 s | ~34% faster |
| MostlyCrossing  | 0.332 s | 0.161 s | ~52% faster |
| CancelHeavy     | 1.946 s | 0.689 s | ~65% faster |
| ManyInstruments | 0.816 s | 0.662 s | ~19% faster |
| LargeSweep      | 0.157 s | 0.068 s | ~57% faster |

Full results:

* [V0.1 baseline](benchmarks/v0.1_baseline.md)
* [V0.2 baseline](benchmarks/v0.2_baseline.md)
* [V0.1 profiling findings](benchmarks/profiling.md)

## Performance Work

V0.1 profiling showed substantial time spent in tree insertion, lookup, and cancellation operations.

V0.2 replaced the original order-oriented tree representation with price-level maps containing FIFO queues and introduced lazy cancellation.

The development process was:

```text
benchmark
-> profile
-> form a hypothesis
-> redesign
-> run correctness tests
-> benchmark again
```

The goal was to make structural performance changes based on measured bottlenecks rather than assumptions.

## Tooling

* C++20
* CMake
* Catch2
* CTest
* GNU gprof
* AddressSanitizer
* UndefinedBehaviorSanitizer
* Python profiling-summary tool
* Git / GitHub
* GitHub Actions

## Status

**V0.2**

The current version includes the matching-engine core, automated correctness testing, deterministic benchmarking, profiling-driven performance work, sanitizer validation, and continuous integration.

