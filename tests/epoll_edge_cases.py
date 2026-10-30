"""
Epoll event-loop edge cases for exchange_server.

Each test spawns the server binary once on a fresh port. These exercise
the non-blocking machinery rather than command semantics: line
reassembly across fragmented TCP deliveries, per-connection input and
output buffers, EPOLLOUT backpressure with the PENDING_OUTPUT_LIMIT
drop, accept-drain under many simultaneous clients, and connection
cleanup.

Usage: epoll_edge_cases.py <path-to-exchange_server>
Registered in CTest as "epoll_edge_cases".
"""

import socket
import subprocess
import sys
import time
import concurrent.futures
import threading


server_path = sys.argv[1]


def get_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def connect(port, rcvbuf=None, timeout=1):
    deadline = time.time() + 2

    while True:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        if rcvbuf is not None:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)

        sock.settimeout(1)

        try:
            sock.connect(("127.0.0.1", port))
            sock.settimeout(timeout)
            return sock
        except OSError:
            sock.close()

            if time.time() >= deadline:
                raise

            time.sleep(0.05)


def receive_lines(sock, count):
    data = b""

    while data.count(b"\n") < count:
        chunk = sock.recv(4096)

        if not chunk:
            raise RuntimeError("Server disconnected unexpectedly")

        data += chunk

    return [line.decode() for line in data.split(b"\n")[:count]]


def expect(actual, expected):
    if actual != expected:
        raise AssertionError(
            f"\n\nExpected: {expected}\n"
            f"Actual:   {actual}"
        )


def expect_closed(sock):
    # The server drops the connection without sending an error. Depending on
    # how much unread data was in flight this surfaces as a clean FIN or a
    # RST, both are acceptable proof of the drop.
    try:
        while True:
            chunk = sock.recv(4096)

            if chunk == b"":
                return
    except ConnectionResetError:
        return


def expect_server_alive(port):
    with connect(port) as client:
        client.sendall(b"TRADES\n")
        reply = receive_lines(client, 1)[0]

        if not reply.isdigit():
            raise AssertionError(
                f"Server not healthy after abusive client: {reply!r}"
            )


def run_test(name, test_function):
    port = get_free_port()

    server = subprocess.Popen(
        [server_path, "--port", str(port)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT
    )

    try:
        test_function(port)
        print(f"PASS {name}")
    except Exception as error:
        print(f"FAIL {name}: {error}")
        failures.append(name)
    finally:
        server.terminate()

        try:
            server.wait(timeout=2)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()


# ---------------------------------------------------------------------
# Input buffering: the epoll loop reassembles lines inside each
# Connection's input_buffer (replacing the old blocking RecvLine path).
# ---------------------------------------------------------------------

def test_partial_command_then_disconnect(port):
    abandoned = connect(port)
    abandoned.sendall(b"ADD 50 100")  # never completed, never flushed
    abandoned.close()

    with connect(port) as client:
        client.sendall(b"GET 50\n")
        expect(receive_lines(client, 1), ["Not Found"])


def test_byte_by_byte_delivery(port):
    with connect(port) as client:
        client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        for byte in b"ADD 42 100 5 BUY LIMIT BTC\n":
            client.sendall(bytes([byte]))
            time.sleep(0.001)

        client.sendall(b"GET 42\n")

        expect(
            receive_lines(client, 2),
            ["Accepted", "42 100 5 Buy Limit BTC"]
        )


def test_pipelined_burst(port):
    commands = []
    expected = []

    for i in range(300):
        order_id = 1000 + i
        commands.append(f"ADD {order_id} 100 1 BUY LIMIT SYM\n")
        commands.append(f"GET {order_id}\n")
        expected.append("Accepted")
        expected.append(f"{order_id} 100 1 Buy Limit SYM")

    with connect(port) as client:
        client.sendall("".join(commands).encode())
        expect(receive_lines(client, len(expected)), expected)


def test_max_line_size_boundary(port):
    # Exactly MAX_LINE_SIZE (256) chars before '\n' -> processed normally.
    with connect(port) as client:
        client.sendall(b"A" * 256 + b"\nGET 1\n")

        expect(
            receive_lines(client, 2),
            ["Invalid Query", "Not Found"]
        )

    # 257 chars before '\n' -> server drops the connection silently.
    client = connect(port)

    try:
        client.sendall(b"A" * 257 + b"\n")
    except (BrokenPipeError, ConnectionResetError):
        pass

    expect_closed(client)
    client.close()

    expect_server_alive(port)


def test_interleaved_partial_commands(port):
    # Per-connection input_buffer isolation: staggered partial writes from
    # several clients must not bleed into each other.
    CLIENTS = 8
    barrier = threading.Barrier(CLIENTS)

    def worker(index):
        order_id = 70000 + index
        first = f"ADD {order_id} 10".encode()
        second = f"0 1 BUY LIMIT S{index}\nGET {order_id}\n".encode()

        with connect(port) as client:
            client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

            barrier.wait()

            client.sendall(first)
            time.sleep(0.02 + 0.01 * index)
            client.sendall(second)

            expect(
                receive_lines(client, 2),
                ["Accepted", f"{order_id} 100 1 Buy Limit S{index}"]
            )

    with concurrent.futures.ThreadPoolExecutor(
        max_workers=CLIENTS
    ) as executor:
        futures = [executor.submit(worker, i) for i in range(CLIENTS)]

        for future in futures:
            future.result()


# ---------------------------------------------------------------------
# Output buffering & backpressure: output_buffer / sent_offset /
# EPOLLOUT toggling / PENDING_OUTPUT_LIMIT.
# ---------------------------------------------------------------------

def test_slow_reader_replies_intact(port):
    PAIRS = 1500

    commands = []
    expected = []

    for i in range(PAIRS):
        order_id = 20000 + i
        commands.append(f"ADD {order_id} 100 1 BUY LIMIT SYM\n")
        commands.append(f"GET {order_id}\n")
        expected.append("Accepted")
        expected.append(f"{order_id} 100 1 Buy Limit SYM")

    payload = "".join(commands).encode()

    # Tiny receive buffer forces the server into partial sends and real
    # output-buffer accumulation instead of instant kernel-buffered writes.
    client = connect(port, rcvbuf=4096, timeout=5)

    try:
        client.sendall(payload)

        data = b""
        reads = 0

        while data.count(b"\n") < len(expected):
            chunk = client.recv(97)  # odd size: splits replies mid-line

            if not chunk:
                raise RuntimeError("Server disconnected mid-stream")

            data += chunk
            reads += 1

            if reads % 40 == 0:
                time.sleep(0.001)

        raw_lines = data.split(b"\n")
        lines = [line.decode() for line in raw_lines[:len(expected)]]

        expect(lines, expected)
        expect(raw_lines[len(expected):], [b""])
    finally:
        client.close()


def test_pending_output_limit_disconnect(port):
    # Client never reads: once queued output exceeds PENDING_OUTPUT_LIMIT
    # (64 KiB) the server must drop the connection. Each "X\n" (2 bytes in)
    # produces "Invalid Query\n" (14 bytes out), so ~10-20 KiB of input
    # backlog is enough once the socket buffers fill.
    client = connect(port, rcvbuf=4096, timeout=0)

    deadline = time.time() + 10
    closed = False

    try:
        while time.time() < deadline and not closed:
            try:
                client.sendall(b"X\n" * 8192)
            except BlockingIOError:
                # Send buffer full - server is applying backpressure.
                time.sleep(0.005)
                continue
            except (BrokenPipeError, ConnectionResetError):
                closed = True
                break

            try:
                if client.recv(4096) == b"":
                    closed = True
            except BlockingIOError:
                pass
            except ConnectionResetError:
                closed = True
    finally:
        client.close()

    if not closed:
        raise AssertionError(
            "Server kept a non-reading client with >64KiB of queued output"
        )

    expect_server_alive(port)


# ---------------------------------------------------------------------
# Connection management: accept-drain loop, connections map churn, and
# engine_mutex serialization under the single-threaded event loop.
# ---------------------------------------------------------------------

def test_many_simultaneous_clients(port):
    CLIENTS = 100
    barrier = threading.Barrier(CLIENTS)

    def worker(index):
        order_id = 50000 + index

        with connect(port) as client:
            barrier.wait()

            client.sendall(
                f"ADD {order_id} 100 1 BUY LIMIT SYM\n"
                f"GET {order_id}\n".encode()
            )

            expect(
                receive_lines(client, 2),
                ["Accepted", f"{order_id} 100 1 Buy Limit SYM"]
            )

    with concurrent.futures.ThreadPoolExecutor(
        max_workers=CLIENTS
    ) as executor:
        futures = [executor.submit(worker, i) for i in range(CLIENTS)]

        for future in futures:
            future.result()


def test_rapid_reconnect_cycles(port):
    for i in range(60):
        order_id = 60000 + i

        with connect(port) as client:
            client.sendall(f"ADD {order_id} 100 1 BUY LIMIT SYM\n".encode())
            expect(receive_lines(client, 1), ["Accepted"])

    # State accumulated across all the short-lived connections.
    with connect(port) as client:
        client.sendall(b"GET 60000\nGET 60059\n")

        expect(
            receive_lines(client, 2),
            [
                "60000 100 1 Buy Limit SYM",
                "60059 100 1 Buy Limit SYM",
            ]
        )


def test_half_close_discards_queued_reply(port):
    # shutdown(SHUT_WR) signals EOF while the command is still in
    # flight. The server drains readable data first, queues the reply,
    # then sees EOF on the next loop iteration and drops the connection
    # before EPOLLOUT can flush - a half-closed client never receives a
    # response. This pins that behavior; honoring half-close would
    # require EPOLLRDHUP handling.
    client = connect(port)

    client.sendall(b"GET 1\n")
    client.shutdown(socket.SHUT_WR)

    expect_closed(client)
    client.close()

    expect_server_alive(port)


def test_concurrent_matching(port):
    # 4 buyers + 4 sellers, 25 orders each, all at price 100: every sell
    # crosses a resting buy, so the trade count is deterministically 100
    # regardless of interleaving.
    PER_SIDE = 4
    ORDERS = 25
    barrier = threading.Barrier(PER_SIDE * 2)

    def worker(index, side):
        base = (80000 if side == "BUY" else 90000) + index * ORDERS

        commands = "".join(
            f"ADD {base + j} 100 1 {side} LIMIT BTC\n"
            for j in range(ORDERS)
        )

        with connect(port) as client:
            barrier.wait()
            client.sendall(commands.encode())
            expect(receive_lines(client, ORDERS), ["Accepted"] * ORDERS)

    with concurrent.futures.ThreadPoolExecutor(
        max_workers=PER_SIDE * 2
    ) as executor:
        futures = [
            executor.submit(worker, i, "BUY")
            for i in range(PER_SIDE)
        ] + [
            executor.submit(worker, i, "SELL")
            for i in range(PER_SIDE)
        ]

        for future in futures:
            future.result()

    with connect(port) as client:
        client.sendall(b"TRADES\n")
        expect(receive_lines(client, 1), [str(PER_SIDE * ORDERS)])


failures = []

run_test("partial_command_then_disconnect", test_partial_command_then_disconnect)
run_test("byte_by_byte_delivery", test_byte_by_byte_delivery)
run_test("pipelined_burst", test_pipelined_burst)
run_test("max_line_size_boundary", test_max_line_size_boundary)
run_test("interleaved_partial_commands", test_interleaved_partial_commands)
run_test("slow_reader_replies_intact", test_slow_reader_replies_intact)
run_test("pending_output_limit_disconnect", test_pending_output_limit_disconnect)
run_test("many_simultaneous_clients", test_many_simultaneous_clients)
run_test("rapid_reconnect_cycles", test_rapid_reconnect_cycles)
run_test("half_close_discards_queued_reply", test_half_close_discards_queued_reply)
run_test("concurrent_matching", test_concurrent_matching)

if failures:
    print(f"\n{len(failures)} test(s) failed: {', '.join(failures)}")
    sys.exit(1)

print("Epoll edge case tests passed")
