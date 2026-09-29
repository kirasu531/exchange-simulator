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


def connect(port):
    deadline = time.time() + 2

    while True:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=1)
            sock.settimeout(1)
            return sock
        except OSError:
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


def run_test(test_function):
    port = get_free_port()

    server = subprocess.Popen(
        [server_path, str(port)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT
    )

    try:
        test_function(port)
    finally:
        server.terminate();

        try:
            server.wait(timeout=2);
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()


def test_concurrent_clients(port):
    client_a = connect(port)
    client_b = connect(port)

    try:
        client_a.sendall(b"ADD 100 100 5 BUY LIMIT BTC\n")
        client_b.sendall(b"ADD 101 200 5 BUY LIMIT ETH\n")

        expect(receive_lines(client_a, 1), ["Accepted"])
        expect(receive_lines(client_b, 1), ["Accepted"])
    finally:
        client_a.close()
        client_b.close()


def basic_test(port):
    # Client 1: add an order.
    with connect(port) as client:
        client.sendall(b"ADD 1 100 5 BUY LIMIT BTC\n")
        expect(receive_lines(client, 1), ["Accepted"])

    # Client 2: engine state must survive Client 1 disconnecting.
    with connect(port) as client:
        client.sendall(
            b"ADD 1 200 3 SELL LIMIT ETH\n"
            b"GET 1\n"
        )

        expect(
            receive_lines(client, 2),
            [
                "Duplicate Id",
                "1 100 5 Buy Limit BTC"
            ]
        )

    # Client 3: matching + cancellation.
    with connect(port) as client:
        client.sendall(
            b"ADD 2 100 2 SELL LIMIT BTC\n"
            b"GET 1\n"
            b"CANCEL 1\n"
            b"GET 1\n"
        )

        expect(
            receive_lines(client, 4),
            [
                "Accepted",
                "1 100 3 Buy Limit BTC",
                "Cancelled",
                "Not Found"
            ]
        )

    # Client 4: deliberately split one command across TCP writes.
    with connect(port) as client:
        client.sendall(b"ADD 10 50")

        time.sleep(0.05)

        client.sendall(
            b" 1 BUY LIMIT AAPL\n"
            b"GET 10\n"
        )

        expect(
            receive_lines(client, 2),
            [
                "Accepted",
                "10 50 1 Buy Limit AAPL"
            ]
        )


def test_concurrent_stress(port):
    CLIENTS = 4
    ORDERS_PER_CLIENT = 100

    barrier = threading.Barrier(CLIENTS)

    def worker(client_id):
        with connect(port) as client:
            commands = []

            for i in range(ORDERS_PER_CLIENT):
                order_id = client_id * 1000 + i

                commands.append(
                    f"ADD {order_id} 100 1 BUY LIMIT BTC\n"
                    f"GET {order_id}\n"
                    f"CANCEL {order_id}\n"
                )

            barrier.wait();

            client.sendall("".join(commands).encode())

            replies = receive_lines(
                client,
                ORDERS_PER_CLIENT * 3
            )

            for i in range(ORDERS_PER_CLIENT):
                order_id = client_id * 1000 + i
                base = i * 3

                expect(replies[base], "Accepted")
                expect(
                    replies[base + 1],
                    f"{order_id} 100 1 Buy Limit BTC"
                )
                expect(replies[base + 2], "Cancelled")

    with concurrent.futures.ThreadPoolExecutor(
        max_workers=CLIENTS
    ) as executor:
        futures = [
            executor.submit(worker, i)
            for i in range(CLIENTS)
        ]

        for future in futures:
            future.result()


run_test(basic_test)
run_test(test_concurrent_clients)
run_test(test_concurrent_stress)

print("Network integration test passed")