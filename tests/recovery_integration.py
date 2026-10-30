"""
Persistence and recovery tests for exchange_server.

Each test writes an exchange command log inside a temporary directory,
starts and stops the server one or more times against it, and verifies
that engine state is reconstructed by replaying the log on startup.
These also pin down which commands are recorded and how malformed log
lines are handled during replay.

Usage: recovery_integration.py <path-to-exchange_server>
Registered in CTest as "recovery_integration".
"""

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
            "--port",
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


def run_test(name, test_function):
    try:
        test_function()
        print(f"PASS {name}")
    except Exception as error:
        print(f"FAIL {name}: {error}")
        failures.append(name)


def test_state_survives_restart():
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


def test_log_records_only_successful_writes():
    # Only commands that actually changed engine state are appended to
    # the log: rejected adds, failed cancels, reads, and garbage leave
    # no trace.
    with tempfile.TemporaryDirectory() as directory:
        log_path = Path(directory) / "exchange.log"
        log_path.touch()

        port = get_free_port()
        server = start_server(port, log_path)

        try:
            with connect(port) as client:
                client.sendall(
                    b"ADD 1 100 5 BUY LIMIT BTC\n"      # logged
                    b"ADD 1 100 5 BUY LIMIT BTC\n"      # duplicate: not logged
                    b"GET 1\n"                          # read: not logged
                    b"CANCEL 9\n"                       # not found: not logged
                    b"CANCEL 1\n"                       # logged
                    b"garbage\n"                        # invalid: not logged
                    b"ADD 2 100 5 SELL LIMIT BTC\n"     # logged
                )

                expect(
                    receive_lines(client, 7),
                    [
                        "Accepted",
                        "Duplicate Id",
                        "1 100 5 Buy Limit BTC",
                        "Not Found",
                        "Cancelled",
                        "Invalid Query",
                        "Accepted",
                    ]
                )
        finally:
            stop_server(server)

        recorded = log_path.read_text().splitlines()

        expect(
            recorded,
            [
                "ADD 1 100 5 BUY LIMIT BTC",
                "CANCEL 1",
                "ADD 2 100 5 SELL LIMIT BTC",
            ]
        )


def test_replay_skips_malformed_lines():
    # RestoreLog parses each record defensively: unknown commands,
    # truncated fields, and extra tokens are skipped line by line.
    with tempfile.TemporaryDirectory() as directory:
        log_path = Path(directory) / "exchange.log"

        # The final line intentionally has no trailing newline:
        # getline() hits EOF while reading it, and RestoreLog treats a
        # newline-less tail as a possibly torn record and skips it.
        log_path.write_text(
            "GARBAGE LINE\n"
            "ADD 1 100 5 BUY LIMIT BTC\n"
            "ADD x 1 2 BUY LIMIT BTC\n"
            "ADD 2 50 5 BUY LIMIT A B\n"
            "CANCEL\n"
            "CANCEL 99\n"
            "TRADES\n"
            "ADD 3 200 1 SELL LIMIT ETH"  # no '\n' -> skipped on replay
        )

        port = get_free_port()
        server = start_server(port, log_path)

        try:
            with connect(port) as client:
                client.sendall(
                    b"GET 1\n"
                    b"GET 2\n"
                    b"GET 3\n"
                    b"TRADES\n"
                )

                expect(
                    receive_lines(client, 4),
                    [
                        "1 100 5 Buy Limit BTC",
                        "Not Found",
                        "Not Found",
                        "0",
                    ]
                )
        finally:
            stop_server(server)


def test_multiple_restarts():
    # The log keeps appending across generations; each restart replays
    # the cumulative history.
    with tempfile.TemporaryDirectory() as directory:
        log_path = Path(directory) / "exchange.log"
        log_path.touch()

        port = get_free_port()

        server = start_server(port, log_path)

        try:
            with connect(port) as client:
                client.sendall(b"ADD 1 100 5 BUY LIMIT BTC\n")
                expect(receive_lines(client, 1), ["Accepted"])
        finally:
            stop_server(server)

        server = start_server(port, log_path)

        try:
            with connect(port) as client:
                client.sendall(b"GET 1\nADD 2 200 5 SELL LIMIT ETH\n")

                expect(
                    receive_lines(client, 2),
                    ["1 100 5 Buy Limit BTC", "Accepted"]
                )
        finally:
            stop_server(server)

        server = start_server(port, log_path)

        try:
            with connect(port) as client:
                client.sendall(
                    b"GET 1\n"
                    b"GET 2\n"
                    b"TRADES\n"
                    b"ADD 3 100 5 BUY LIMIT ETH\n"
                    b"GET 3\n"
                )

                expect(
                    receive_lines(client, 5),
                    [
                        "1 100 5 Buy Limit BTC",
                        "2 200 5 Sell Limit ETH",
                        "0",
                        "Accepted",
                        "3 100 5 Buy Limit ETH",
                    ]
                )
        finally:
            stop_server(server)


failures = []

run_test("state_survives_restart", test_state_survives_restart)
run_test(
    "log_records_only_successful_writes",
    test_log_records_only_successful_writes
)
run_test("replay_skips_malformed_lines", test_replay_skips_malformed_lines)
run_test("multiple_restarts", test_multiple_restarts)

if failures:
    print(f"\n{len(failures)} test(s) failed: {', '.join(failures)}")
    sys.exit(1)

print("Recovery integration tests passed")
