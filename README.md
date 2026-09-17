# C++ Exchange Simulator

## About

This project is a multi-instrument exchange matching engine written in C++20.

I started it to move beyond competitive-programming-style C++ and learn practical software engineering and systems programming.

The project is intentionally developed incrementally. Instead of trying to design the final system in advance, I build a working version, encounter engineering problems, learn the relevant concepts, and improve the system.

The project is currently being used to learn topics such as:

- multi-file C++
- interfaces and encapsulation
- CMake
- Git
- automated testing
- deterministic systems
- benchmarking
- profiling
- performance-oriented data structures
- debugging and sanitizers
- CI

---

# Current Status

## V0.1 — Matching Engine Core

Completed.

V0.1 focuses mainly on correctness, architecture, and automated testing.

## V0.2 — Performance and Architecture

In progress.

The goal of V0.2 is:

```text
measure
-> profile
-> identify bottlenecks
-> redesign where justified
-> measure again
```

The aim is to make performance changes based on evidence rather than intuition.

---

# Features

The engine currently supports:

- multiple instruments
- limit orders
- market orders
- buy and sell sides
- price-time priority
- partial fills
- one incoming order matching multiple resting orders
- order cancellation
- globally unique order IDs
- deterministic arrival sequencing
- globally unique deterministic trade sequencing
- order validation
- explicit AddOrder / CancelOrder result values
- active-order lookup using std::optional
- trade history
- Catch2 automated tests
- CTest integration
- deterministic benchmark workloads

---

# Order

An order contains:

```text
orderId
arrivalSequence
price
quantity
side
type
instrument
```

Side can be:

```text
Buy
Sell
```

Order type can be:

```text
Limit
Market
```

`arrivalSequence` is assigned internally by `MatchingEngine`.

The caller does not decide order priority.

If two orders have the same price, the one with the lower arrival sequence has priority.

---

# Trade

A trade contains:

```text
tradeSequence
buyerId
sellerId
instrument
price
quantity
```

`tradeSequence` is globally unique and deterministic.

The execution price is the price of the resting order.

---

# High-Level Architecture

```text
CLI
 |
 v
MatchingEngine
 |
 +---- BTC  ----> OrderBook
 |
 +---- AAPL ----> OrderBook
 |
 +---- ...  ----> OrderBook
                    |
                    v
                  Trades
```

## MatchingEngine

`MatchingEngine` is the public entry point of the exchange.

Its responsibilities include:

- validating incoming orders
- rejecting duplicate order IDs
- assigning arrival sequences
- routing orders to the correct instrument
- cancelling orders
- looking up active orders
- maintaining global trade sequencing
- collecting trades from different order books

Main operations:

```text
AddOrder(order)
CancelOrder(orderId)
GetOrder(orderId)
GetTrades()
PrintTrades()
```

## OrderBook

Each `OrderBook` represents one instrument.

It is responsible for:

- storing resting buy and sell orders
- price priority
- FIFO priority at equal prices
- matching incoming orders
- partial fills
- removing cancelled orders
- producing trades

The internal containers are private implementation details.

This is important because the internal representation may change during V0.2 without changing the external MatchingEngine API.

---

# Matching Rules

For limit orders:

```text
BUY price >= best SELL price
    -> trade

SELL price <= best BUY price
    -> trade
```

Buy orders prioritize:

```text
higher price first
then earlier arrival
```

Sell orders prioritize:

```text
lower price first
then earlier arrival
```

Market orders ignore their own price and consume available liquidity from the best available prices.

Any unfilled remainder of a market order disappears.

Any unfilled remainder of a limit order stays in the order book.

---

# Project Structure

The project is roughly organized as:

```text
include/
    order_book.hpp
    matching_engine.hpp
    trade.hpp
    ...

src/
    main.cpp
    order_book.cpp
    matching_engine.cpp
    ...

tests/
    order_book_tests.cpp

bench/
    benchmark.cpp
    benchmark.hpp

benchmarks/
    v0.1_baseline.md
    profiling.md

CMakeLists.txt
README.md
```

Header files (`.hpp`) mainly describe types and interfaces.

Source files (`.cpp`) contain implementations.

---

# CMake Refresher

CMake describes how the C++ project should be built.

The important concepts used in this project are:

## add_library

The matching-engine implementation is compiled into reusable code:

```text
exchange_core
```

This contains logic such as:

```text
OrderBook
MatchingEngine
```

Both the real executable and the test executable can reuse it.

Conceptually:

```text
                exchange
               /
exchange_core
               \
                exchange_tests
```

## add_executable

Creates a runnable program.

For example:

```text
exchange
exchange_tests
exchange_bench
```

## target_link_libraries

Declares that one target depends on another.

For example:

```text
exchange -> exchange_core
```

means the executable uses code compiled into `exchange_core`.

## target_include_directories

Tells the compiler where project headers can be found.

For example:

```text
include/
```

allows code to write:

```cpp
#include "matching_engine.hpp"
```

instead of using relative paths such as:

```cpp
#include "../include/matching_engine.hpp"
```

---

# Building

There are several independent build directories.

They all compile the same source code, but for different purposes.

## Development Build

Used during normal coding and automated testing.

Configure:

```bash
cmake -S . -B build
```

Meaning:

```text
-S .
    source directory = current directory

-B build
    generated build files go into ./build
```

Compile:

```bash
cmake --build build
```

Run:

```bash
./build/exchange
```

Normally, after changing only C++ source files, it is enough to run:

```bash
cmake --build build
```

CMake performs an incremental build and only recompiles what changed.

---

# Release Build

Used for real performance measurements.

Configure:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
```

Compile:

```bash
cmake --build build-release -j
```

`Release` enables compiler optimizations.

`-j	` allows independent compilation jobs to run in parallel.

The Release build should be used for benchmark numbers:

```bash
./build-release/exchange_bench
```

Do not compare benchmark results from different build configurations.

---

# Profiling Build

Used with `gprof`.

This build contains profiling instrumentation and is therefore not used for real benchmark timings.

Example configuration:

```bash
cmake -S . -B build-profile \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-pg" \
    -DCMAKE_EXE_LINKER_FLAGS="-pg"
```

Compile:

```bash
cmake --build build-profile -j
```

Run a profiling workload:

```bash
./build-profile/exchange_bench CancelHeavy 500000
```

This produces:

```text
gmon.out
```

Convert it into a readable report:

```bash
gprof ./build-profile/exchange_bench gmon.out > profile.txt
```

Important:

```text
build-release = measure how fast the engine is

build-profile = investigate where the engine spends its work
```

Profiling runtime itself should not be used as benchmark data.

---

# Testing

The project uses Catch2 for writing tests and CTest for running them.

Catch2 provides constructs such as:

```text
TEST_CASE
REQUIRE
```

CTest discovers and runs those tests.

Build the development version:

```bash
cmake --build build
```

Run all tests:

```bash
ctest --test-dir build --output-on-failure
```

`--output-on-failure` prints detailed output when a test fails.

The test suite checks behavior through the public `MatchingEngine` API.

Current tests cover:

- valid and invalid orders
- operation-result enums
- duplicate IDs
- duplicate IDs across instruments
- deterministic arrival sequences
- non-crossing orders
- instrument isolation
- exact and partial fills
- best bid / best ask priority
- FIFO priority
- multi-order matching
- limit vs market behavior
- cancellation
- fully filled orders
- deterministic global trade sequencing
- trade logs

---

# Benchmarking

The project contains a separate benchmark executable.

The benchmark uses a fixed random seed:

```text
1337
```

This makes workloads deterministic.

The same version of a workload can therefore be replayed against different engine implementations.

Current workloads include:

```text
Resting
Random
MostlyCrossing
CancelHeavy
ManyInstruments
LargeSweep
```

The benchmark records:

- number of timed engine operations
- median execution time
- number of runs
- number of generated trades

Trade count is also used as a basic correctness check when comparing different implementations.

Benchmark input generation happens outside the timed region.

For repeated measurements, the same generated workload is replayed against fresh `MatchingEngine` instances.

---

# V0.1 Performance Baseline

The V0.1 benchmark results are stored separately under:

```text
benchmarks/
```

These measurements are kept unchanged so V0.2 can be compared against the same workloads.

Performance comparisons should use:

```text
same machine
same Release configuration
same seed
same workload
same operation count
```

---

# Profiling

V0.2 profiling currently uses GNU `gprof`.

Initial profiling of the cancellation-heavy workload showed significant cost in red-black-tree operations used by the current `std::set` / `std::map` based implementation.

Important observed hotspots include:

```text
order-tree lookup / erase
order-tree insertion
OrderBook::AddOrder
MatchingEngine::AddOrder
MatchingEngine::CancelOrder
```

This currently suggests that tree operations may be worth investigating.

However, no major redesign should be made based on one workload alone.

Other workloads are profiled before deciding what optimization is justified.

---

# V0.2 Plan

Current progression:

```text
V0.1 benchmark baseline
        |
        v
profile several workloads
        |
        v
identify repeated bottlenecks
        |
        v
form optimization hypothesis
        |
        v
change data structure / architecture
        |
        v
run correctness tests
        |
        v
run identical benchmarks
        |
        v
compare before / after
```

Possible areas of investigation include:

- price-level order-book representation
- faster cancellation using stored handles / iterators
- reducing duplicated order state
- reducing tree lookups
- improving memory locality
- avoiding unnecessary copies

These are hypotheses, not predetermined optimizations.

---

# Development Philosophy

The project intentionally follows:

```text
build
-> encounter a limitation
-> understand it
-> improve the design
-> test
-> measure
```

The goal is not just to produce a matching engine, but to understand the engineering decisions behind it.

Notes:
ASan on this Ubuntu 22.04 system was intermittently crashing at startup because of ASLR/runtime interaction.
Tests are run with ASLR disabled for the sanitizer process via setarch -R.
