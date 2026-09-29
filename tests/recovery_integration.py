import socket
import subprocess
import sys
import time
import tempfile
from pathlib import Path


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
            f"\nExpected: {expected}\n"
            f"Actual:   {actual}"
        )

def start_server(port, log_path):
    return subprocess.Popen(
        [
            server_path,
            str(port),
            "--log",
            str(log_path)
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT
    )


def stop_server(server):
    server.terminate()

    try:
        server.wait(timeout=2)
    except subprocess.TimeoutExpired:
        server.kill()
        server.wait()


with tempfile.TemporaryDirectory() as directory:
    log_path = Path(directory) / "exchange.log"
    log_path.touch()

    port = get_free_port()

    # --------------------------------------------------
    # First server run
    # --------------------------------------------------

    server = start_server(port, log_path)

    try:
        with connect(port) as client:
            client.sendall(
                b"ADD 1 100 10 BUY LIMIT BTC\n"
                b"ADD 2 105 5 SELL LIMIT BTC\n"
                b"ADD 3 99 7 BUY LIMIT BTC\n"
                b"ADD 4 100 4 SELL LIMIT BTC\n"
                b"CANCEL 3\n"
                b"ADD 5 101 8 SELL LIMIT BTC\n"
                b"ADD 6 110 20 BUY LIMIT BTC\n"
            )

            expect(
                receive_lines(client, 7),
                [
                    "Accepted",
                    "Accepted",
                    "Accepted",
                    "Accepted",
                    "Cancelled",
                    "Accepted",
                    "Accepted"
                ]
            )

    finally:
        stop_server(server)

    # --------------------------------------------------
    # Restart using the SAME log
    # --------------------------------------------------

    server = start_server(port, log_path)

    try:
        with connect(port) as client:
            client.sendall(
                b"GET 1\n"
                b"GET 3\n"
                b"GET 6\n"
                b"TRADES\n"
            )

            expect(
                receive_lines(client, 4),
                [
                    "1 100 6 Buy Limit BTC",
                    "Not Found",
                    "6 110 7 Buy Limit BTC",
                    "3"
                ]
            )

            # Continue operating after recovery.
            client.sendall(
                b"ADD 7 109 5 SELL LIMIT BTC\n"
                b"ADD 8 110 2 SELL LIMIT BTC\n"
                b"TRADES\n"
                b"GET 6\n"
            )

            expect(
                receive_lines(client, 4),
                [
                    "Accepted",
                    "Accepted",
                    "5",
                    "Not Found"
                ]
            )

    finally:
        stop_server(server)


print("Recovery integration test passed")