"""
Command-line argument validation tests for exchange_server.

No sockets involved: each case spawns the server binary directly and
checks whether it exits with code 1 (invalid arguments) or stays
running (valid arguments). For one valid case a real connection is
made to prove the server is actually serving, not merely alive.

Usage: server_cli.py <path-to-exchange_server>
Registered in CTest as "server_cli".
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


def expect_rejected(args):
    # Invalid arguments must terminate the process with exit code 1
    # before it ever binds a socket.
    server = subprocess.Popen(
        [server_path] + args,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    try:
        code = server.wait(timeout=2)
    except subprocess.TimeoutExpired:
        server.kill()
        server.wait()
        raise AssertionError(
            f"Server kept running with invalid args: {args}"
        )

    if code != 1:
        raise AssertionError(
            f"Args {args}: expected exit code 1, got {code}"
        )


def expect_running(args, check_connect=False):
    server = subprocess.Popen(
        [server_path] + args,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    try:
        time.sleep(0.3)

        if server.poll() is not None:
            raise AssertionError(
                f"Server exited ({server.returncode}) "
                f"with valid args: {args}"
            )

        if check_connect:
            port = int(args[args.index("--port") + 1])

            deadline = time.time() + 2

            while True:
                try:
                    with socket.create_connection(
                        ("127.0.0.1", port), timeout=1
                    ):
                        break
                except OSError:
                    if time.time() >= deadline:
                        raise

                    time.sleep(0.05)
    finally:
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


def test_rejects_invalid_arguments():
    with tempfile.TemporaryDirectory() as directory:
        log_path = Path(directory) / "exchange.log"

        bad_args = [
            ["--port"],                                  # missing value
            ["--port", "0"],                             # below range
            ["--port", "65536"],                         # above range
            ["--port", "abc"],                           # non-numeric
            ["--port", "40x"],                           # trailing garbage
            ["--port", "-1"],                            # negative
            ["--port", "4000", "--port"],                # missing value
            ["--port", "4000", "extra"],                 # stray positional
            ["4000"],                                    # positional port
            ["--log"],                                   # missing value
            ["--log", "--port"],                         # flag as value
            ["--sync-interval-ms"],                      # missing value
            ["--sync-interval-ms", "0"],                 # not positive
            ["--sync-interval-ms", "-5"],                # negative
            ["--sync-interval-ms", "abc"],               # non-numeric
            ["--sync-interval-ms", "10"],                # requires --log
            ["--frobnicate"],                            # unknown flag
        ]

        for args in bad_args:
            expect_rejected(args)


def test_accepts_valid_arguments():
    expect_running(
        ["--port", str(get_free_port())],
        check_connect=True
    )

    with tempfile.TemporaryDirectory() as directory:
        expect_running(
            [
                "--port", str(get_free_port()),
                "--log", str(Path(directory) / "a.log"),
            ]
        )

        expect_running(
            [
                "--port", str(get_free_port()),
                "--log", str(Path(directory) / "b.log"),
                "--sync-interval-ms", "50",
            ]
        )


failures = []

run_test("rejects_invalid_arguments", test_rejects_invalid_arguments)
run_test("accepts_valid_arguments", test_accepts_valid_arguments)

if failures:
    print(f"\n{len(failures)} test(s) failed: {', '.join(failures)}")
    sys.exit(1)

print("Server CLI tests passed")
