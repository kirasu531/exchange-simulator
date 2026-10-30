# C++ Exchange Simulator

A multi-instrument matching engine and non-blocking TCP server written in C++20, developed as a systems-programming learning project.

The project progresses through matching-engine correctness, performance profiling, TCP networking, persistence, and a single-threaded `epoll` event loop. It is an experimental simulator, not a production exchange.

## Features

### Matching engine

- Limit and market orders
- Buy and sell sides
- Multiple instruments
- Price-time priority
- FIFO ordering within each price level
- Partial fills and multi-order matching
- Lazy cancellation: cancelled orders stay in their price-level queue and are skipped during matching
- Globally unique order IDs — an id is never reusable, even after the order is cancelled or fully filled
- Deterministic arrival and trade sequencing
- Active-order lookup and global trade history

### Network server (V0.5)

- Single-threaded `epoll` event loop; all clients share one loop
- Non-blocking accept-drain via `accept4()` with `SOCK_NONBLOCK | SOCK_CLOEXEC`
- Per-connection input and output buffers; commands are reassembled across arbitrary TCP fragmentation
- `EPOLLOUT` interest toggled only while a connection has queued output
- Bounded I/O per event (`READ_BUDGET` / `SEND_BUDGET`, 4 KiB each)
- Backpressure protection: connections are dropped when queued output exceeds `PENDING_OUTPUT_LIMIT` (64 KiB) or a line exceeds `MAX_LINE_SIZE` (256 bytes)
- Newline-delimited text protocol with CRLF tolerance
- Input validation and error responses

### Persistence (V0.4)

- Optional append-only logging of successful state-changing commands
- Recovery on server restart by replaying the log; malformed lines and a torn trailing record are skipped
- Configurable periodic `fdatasync()` in a background thread
- Recovery integration tests and durability/performance benchmarks

## Architecture

```text
                 TCP clients
               /      |      \
              /       |       \
        [accept4]  conn fd   conn fd          <- one epoll_wait loop
              \       |       /
        input_buffer -> ParseCommand -> verdict -> output_buffer
                        (per-connection state, EPOLLIN/EPOLLOUT)
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

With `--log`, successful state changes are appended to a command log under a mutex. An optional background thread periodically calls `fdatasync()` when the log is dirty. On startup, the server replays recorded commands to reconstruct engine state (including regenerated trade sequences).

## TCP Protocol

Commands are UTF-8/ASCII text terminated by `\n` (a trailing `\r` is stripped).

```text
ADD <orderId> <price> <quantity> <BUY|SELL> <LIMIT|MARKET> <instrument>
CANCEL <orderId>
GET <orderId>
TRADES
```

Examples:

```text
ADD 1 100 5 BUY LIMIT BTC
ADD 2 0 10 SELL MARKET BTC
GET 1
CANCEL 1
TRADES
```

Replies:

```text
Accepted                                 order accepted
Duplicate Id                             orderId already registered
Cancelled                                order cancelled
Not Found                                order not active (GET / CANCEL)
Invalid Query                            malformed or invalid command
<orderId> <price> <qty> <Side> <Type> <instrument>    GET response
<count>                                  TRADES response
Persistence Error                        log write failed; connection closed
```

The server reconstructs complete commands even when TCP splits or combines transmitted byte chunks. A connection sending a line longer than 256 bytes, or accumulating more than 64 KiB of unread output, is dropped without an error reply.

## Project Structure

```text
include/        public types and interfaces
src/            matching engine, CLI, epoll TCP server/client, I/O helpers
tests/          Catch2 unit tests and Python integration suites
benchmarks/     engine, network, and durability benchmarks and results
profiles/       summarized profiling results
scripts/        build, test, benchmark, and profiling helpers
tools/          auxiliary tooling
.github/        GitHub Actions CI
```

## Build

Requirements:

- CMake 3.20+
- C++20 compiler
- Python 3 for the integration test suites

```bash
cmake -S . -B build
cmake --build build -j 4
```

Run the original CLI:

```bash
./build/exchange
```

### Server options

```text
--port <1-65535>          listening port (default: 4000)
--log <path>              enable append-only command logging
--sync-interval-ms <n>    periodic fdatasync interval; requires --log
```

Without `--log`, persistence is disabled:

```bash
./build/exchange_server --port 4000
```

With a log, successful state-changing commands are recorded; without an interval there is **no explicit periodic disk sync**:

```bash
./build/exchange_server --port 4000 --log exchange.log
```

To request a sync approximately every 10 ms:

```bash
./build/exchange_server --port 4000 --log exchange.log --sync-interval-ms 10
```

The interval is configurable, not universally optimal. Commands may be acknowledged before they are synchronized, so sudden machine failure can lose recently acknowledged commands. Relative log paths depend on the server process's working directory.

### Client

An interactive client using the same epoll/buffer machinery; commands typed on stdin are sent, replies are printed:

```bash
./build/exchange_client --port 4000
```

## Testing

Run the complete CTest suite:

```bash
ctest --test-dir build --output-on-failure
```

Suites:

| Name | What it covers |
|---|---|
| Catch2 `[MatchingEngine]` | validation, priority, partial fills, market orders, lazy cancellation, sticky ids, trade log |
| Catch2 `[network_utils]` | `Send`/`Recv`/`SendAll`/`WriteAll`/`RecvLine` and `Connection` buffering over `socketpair()` |
| `network_integration` | end-to-end TCP flow, state shared across clients, concurrent stress |
| `server_protocol` | command parsing, exact replies, whitespace/CRLF, malformed input, wire-visible engine semantics |
| `epoll_edge_cases` | fragmented delivery, pipelining, backpressure limits, client churn, half-close behavior |
| `recovery_integration` | log contents, restart replay, malformed-line and torn-tail handling, multi-restart |
| `server_cli` | command-line argument validation |

Additional validation is performed with:

- AddressSanitizer
- UndefinedBehaviorSanitizer
- ThreadSanitizer

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
cmake --build build-release -j 4
./build-release/exchange_bench
```

### Network benchmark

The network benchmark uses the same workload definitions but sends operations through the TCP server and waits for each command response.

```bash
./scripts/network_benchmark.sh
```

It starts a fresh server for each run and reports the median of repeated runs.

### V0.4 durability experiments

V0.4 compares append-only logging, synchronization after every command, isolated write-and-`fdatasync()` batching, and periodic synchronization. The results illustrate a throughput-versus-durability tradeoff rather than a universal optimal interval.

For 20,000 operations on the measured machine, a **10 ms** periodic sync interval achieved approximately **57.5k operations/s** (`Resting`) and **56.3k operations/s** (`Random`). These are local loopback measurements, not production throughput guarantees.

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
- [V0.4 initial durability results](benchmarks/v0.4_durability_initial.md)
- [V0.4 fdatasync batching results](benchmarks/v0.4_fdatasync_batch_results.md)
- [V0.4 periodic sync results](benchmarks/v0.4_periodic_sync_results.md)

## Version Progression

```text
V0.1  Matching-engine correctness and automated testing
  ↓
V0.2  Profiling, performance redesign, sanitizers, and CI
  ↓
V0.3  TCP networking, concurrent clients, integration testing,
      ThreadSanitizer validation, and network benchmarking
  ↓
V0.4  Append-only persistence, recovery, periodic fdatasync,
      and durability/performance experiments
  ↓
V0.5  Single-threaded epoll event loop: non-blocking I/O,
      per-connection buffering, EPOLLOUT backpressure,
      and protocol/edge-case/CLI test suites
```

## Known Limitations

- A client that half-closes with `shutdown(SHUT_WR)` never receives queued replies; the server drops the connection on EOF before flushing output (no `EPOLLRDHUP` handling).
- Order IDs are never reusable — a cancelled or filled id stays registered forever.
- Cancelled orders occupy their price-level queues until a crossing match skips them, and `cancelled_orders` grows without bound over the engine's lifetime.
- A log record written without a trailing newline (e.g. a torn final write) is skipped during replay.
- Single-threaded: the event loop handles all clients on one thread; `engine_mutex` remains from the threaded design.

## Tooling

- C++20
- CMake
- Catch2 / CTest
- POSIX sockets, `epoll`, and file I/O (`accept4`, `recv`/`send`, `write`, `fdatasync`)
- C++ threads and mutexes
- GNU gprof
- ASan / UBSan / TSan
- Python integration tests
- Bash automation
- Git / GitHub
- GitHub Actions

## Status

**V0.5**

The current version provides a single-threaded `epoll`-driven TCP matching engine with per-connection buffering and backpressure limits, optional persistent command logging, startup recovery, and configurable periodic disk synchronization. It remains an educational system; crash-atomic transactions, production-grade failure recovery, and multi-threaded matching are outside its scope.
