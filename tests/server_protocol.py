"""
Text protocol and command validation tests for exchange_server.

Each test spawns the server binary once on a fresh port. These pin down
the wire contract - ADD/CANCEL/GET/TRADES parsing, exact reply strings,
whitespace and CRLF handling, malformed input, and engine semantics
visible through replies (fills, trade count, sticky order ids) -
independently of the epoll machinery underneath.

Usage: server_protocol.py <path-to-exchange_server>
Registered in CTest as "server_protocol".
"""

import socket
import subprocess
import sys
import time


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
# Command validation: request -> reply semantics. These tests exercise
# ParseCommand/tokenize/HandleCommand and are independent of the
# transport implementation underneath.
# ---------------------------------------------------------------------

def test_empty_and_whitespace_lines(port):
    with connect(port) as client:
        client.sendall(b"\n   \n\t\n")

        expect(
            receive_lines(client, 3),
            ["Invalid Query"] * 3
        )

        # Connection must still be usable.
        client.sendall(b"TRADES\n")

        if not receive_lines(client, 1)[0].isdigit():
            raise AssertionError("Connection died after empty lines")


def test_surrounding_whitespace(port):
    with connect(port) as client:
        client.sendall(
            b"   ADD 9 10 2 BUY LIMIT BTC   \n"
            b"  GET   9  \n"
        )

        expect(
            receive_lines(client, 2),
            ["Accepted", "9 10 2 Buy Limit BTC"]
        )


def test_malformed_commands(port):
    bad_commands = [
        b"ADD",                            # missing everything
        b"ADD 1 100 5 BUY LIMIT",          # missing instrument
        b"ADD 1 100 5 BUY LIMIT BTC X",    # extra field
        b"ADD x 100 5 BUY LIMIT BTC",      # non-numeric id
        b"ADD 1 x 5 BUY LIMIT BTC",        # non-numeric price
        b"ADD 1 100 x BUY LIMIT BTC",      # non-numeric quantity
        b"ADD -1 100 5 BUY LIMIT BTC",     # negative id
        b"ADD 1 -5 5 BUY LIMIT BTC",       # negative limit price
        b"ADD 1 100 0 BUY LIMIT BTC",      # zero quantity
        b"ADD 1 100 5 buy LIMIT BTC",      # lowercase side
        b"ADD 1 100 5 BUY limit BTC",      # lowercase type
        b"add 1 100 5 BUY LIMIT BTC",      # lowercase command
        b"CANCEL",                         # missing id
        b"CANCEL x",                       # non-numeric id
        b"CANCEL -1",                      # negative id
        b"CANCEL 1 2",                     # extra field
        b"GET",                            # missing id
        b"GET x",                          # non-numeric id
        b"GET -1",                         # negative id
        b"GET 1 2",                        # extra field
        b"TRADES x",                       # extra field
        b"GET\t1",                         # tab is not a separator
        b"FOOBAR",                         # unknown command
    ]

    payload = b"".join(command + b"\n" for command in bad_commands)

    with connect(port) as client:
        client.sendall(payload)

        expect(
            receive_lines(client, len(bad_commands)),
            ["Invalid Query"] * len(bad_commands)
        )

        # Connection survives a burst of garbage.
        client.sendall(b"TRADES\n")

        if not receive_lines(client, 1)[0].isdigit():
            raise AssertionError("Connection died after malformed commands")


def test_market_order_ignores_price(port):
    # IsValidOrder only checks price for LIMIT orders. A MARKET order with
    # price 0 is accepted; documented here so a change is noticed.
    with connect(port) as client:
        client.sendall(
            b"ADD 3 0 5 BUY MARKET BTC\n"
            b"GET 3\n"
        )

        replies = receive_lines(client, 2)

        expect(replies[0], "Accepted")

        if replies[1] == "Not Found":
            # The market order had nothing to match against, so it either
            # rested or was dropped - both are plausible, but the ADD verdict
            # is what this test pins down.
            pass


def test_crlf_line_endings(port):
    with connect(port) as client:
        # Trailing '\r' is stripped before parsing: CRLF clients work.
        client.sendall(b"GET 5\r\n")
        expect(receive_lines(client, 1), ["Not Found"])

        # The instrument is stored clean - no '\r' leak.
        client.sendall(
            b"ADD 5 100 5 BUY LIMIT BTC\r\n"
            b"GET 5\n"
        )

        expect(
            receive_lines(client, 2),
            ["Accepted", "5 100 5 Buy Limit BTC"]
        )

        # CRLF works on retrieval too.
        client.sendall(b"GET 5\r\n")
        expect(receive_lines(client, 1), ["5 100 5 Buy Limit BTC"])


def test_no_response_until_newline(port):
    with connect(port) as client:
        client.sendall(b"GET 3")  # no newline yet

        client.settimeout(0.4)

        try:
            data = client.recv(4096)
            raise AssertionError(f"Server answered a partial line: {data!r}")
        except socket.timeout:
            pass
        finally:
            client.settimeout(1)

        client.sendall(b"\n")
        expect(receive_lines(client, 1), ["Not Found"])


def test_binary_garbage(port):
    with connect(port) as client:
        client.sendall(b"\x00\xff\xfe\x01\nGET 1\n")

        expect(
            receive_lines(client, 2),
            ["Invalid Query", "Not Found"]
        )


# ---------------------------------------------------------------------
# Engine semantics over the wire: here the reply CONTENT is the
# contract - fills reflected in GET, the TRADES counter, sticky ids.
# ---------------------------------------------------------------------

def test_matching_flow_and_trades_count(port):
    with connect(port) as client:
        client.sendall(
            b"ADD 10 100 5 BUY LIMIT BTC\n"
            b"ADD 11 100 3 SELL LIMIT BTC\n"
            b"TRADES\n"
            b"GET 10\n"
            b"GET 11\n"
        )

        expect(
            receive_lines(client, 5),
            [
                "Accepted",
                "Accepted",
                "1",                          # one trade happened
                "10 100 2 Buy Limit BTC",     # buy rests with remainder
                "Not Found",                  # sell fully filled
            ]
        )


def test_duplicate_id_is_sticky(port):
    # Once an id is registered it is never released: re-adding it is
    # rejected even after the original order was cancelled.
    with connect(port) as client:
        client.sendall(
            b"ADD 20 100 5 BUY LIMIT BTC\n"
            b"ADD 20 100 5 SELL LIMIT BTC\n"
            b"CANCEL 20\n"
            b"ADD 20 100 5 SELL LIMIT BTC\n"
            b"GET 20\n"
        )

        expect(
            receive_lines(client, 5),
            [
                "Accepted",
                "Duplicate Id",
                "Cancelled",
                "Duplicate Id",
                "Not Found",
            ]
        )


def test_integer_boundaries(port):
    with connect(port) as client:
        client.sendall(
            b"ADD 0 100 5 BUY LIMIT BTC\n"           # id 0 is valid
            b"ADD 2147483647 100 5 BUY LIMIT BTC\n"  # INT_MAX id
            b"ADD 2147483648 100 5 BUY LIMIT BTC\n"  # overflows int
            b"ADD 21 2147483647 5 BUY LIMIT BTC\n"   # INT_MAX price
            b"GET 21\n"
        )

        expect(
            receive_lines(client, 5),
            [
                "Accepted",
                "Accepted",
                "Invalid Query",
                "Accepted",
                "21 2147483647 5 Buy Limit BTC",
            ]
        )


def test_get_reflects_partial_fill(port):
    with connect(port) as client:
        client.sendall(
            b"ADD 30 100 10 SELL LIMIT BTC\n"
            b"ADD 31 100 4 BUY LIMIT BTC\n"
            b"GET 30\n"
            b"GET 31\n"
            b"TRADES\n"
        )

        expect(
            receive_lines(client, 5),
            [
                "Accepted",
                "Accepted",
                "30 100 6 Sell Limit BTC",
                "Not Found",
                "1",
            ]
        )


failures = []

run_test("empty_and_whitespace_lines", test_empty_and_whitespace_lines)
run_test("surrounding_whitespace", test_surrounding_whitespace)
run_test("malformed_commands", test_malformed_commands)
run_test("market_order_ignores_price", test_market_order_ignores_price)
run_test("crlf_line_endings", test_crlf_line_endings)
run_test("no_response_until_newline", test_no_response_until_newline)
run_test("binary_garbage", test_binary_garbage)
run_test("matching_flow_and_trades_count", test_matching_flow_and_trades_count)
run_test("duplicate_id_is_sticky", test_duplicate_id_is_sticky)
run_test("integer_boundaries", test_integer_boundaries)
run_test("get_reflects_partial_fill", test_get_reflects_partial_fill)

if failures:
    print(f"\n{len(failures)} test(s) failed: {', '.join(failures)}")
    sys.exit(1)

print("Server protocol tests passed")
