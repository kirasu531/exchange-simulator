"""
Market-data subscription tests for exchange_server (v0.6).

Each test spawns the server binary once on a fresh port. Covers the
SUBSCRIBE/UNSUBSCRIBE commands and their replies, EVENT TRADE delivery
and fan-out to multiple subscribers, per-instrument filtering, event
ordering and interleaving with normal request/reply traffic, and
subscription cleanup on disconnect.

Usage: market_data.py <path-to-exchange_server>
Registered in CTest as "market_data".
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


def expect_silence(sock, seconds=0.3):
    # Assert that no data arrives within `seconds`. Used to prove a
    # subscriber receives NO events it shouldn't get.
    sock.settimeout(seconds)

    try:
        data = sock.recv(4096)
    except socket.timeout:
        return
    except (BrokenPipeError, ConnectionResetError):
        raise AssertionError("Connection dropped unexpectedly")
    finally:
        sock.settimeout(1)

    raise AssertionError(f"Expected silence, got: {data!r}")


def expect_server_alive(port):
    with connect(port) as client:
        client.sendall(b"TRADES\n")
        reply = receive_lines(client, 1)[0]

        if not reply.isdigit():
            raise AssertionError(f"Server not healthy: {reply!r}")


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
# Command surface: SUBSCRIBE / UNSUBSCRIBE parsing and replies.
# ---------------------------------------------------------------------

def test_subscribe_unsubscribe_replies(port):
    with connect(port) as client:
        client.sendall(
            b"SUBSCRIBE BTC\n"
            b"SUBSCRIBE BTC\n"          # duplicate
            b"SUBSCRIBE ETH\n"          # second instrument
            b"UNSUBSCRIBE BTC\n"
            b"UNSUBSCRIBE BTC\n"        # already removed
            b"UNSUBSCRIBE XRP\n"        # never subscribed
            b"SUBSCRIBE BTC\n"          # resubscribe
        )

        expect(
            receive_lines(client, 7),
            [
                "Subscribed",
                "Already subscribed",
                "Subscribed",
                "Unsubscribed",
                "Not Subscribed",
                "Not Subscribed",
                "Subscribed",
            ]
        )


def test_subscribe_arg_validation(port):
    with connect(port) as client:
        client.sendall(
            b"SUBSCRIBE\n"              # missing instrument
            b"SUBSCRIBE A B\n"          # extra field
            b"UNSUBSCRIBE\n"            # missing instrument
            b"UNSUBSCRIBE A B\n"        # extra field
            b"TRADES\n"
        )

        replies = receive_lines(client, 5)

        expect(replies[:4], ["Invalid Query"] * 4)

        if not replies[4].isdigit():
            raise AssertionError("Connection died after bad subscribe args")


# ---------------------------------------------------------------------
# Event delivery: fan-out, filtering, ordering, interleaving.
# ---------------------------------------------------------------------

def test_basic_event_delivery(port):
    with connect(port) as subscriber, connect(port) as trader:
        subscriber.sendall(b"SUBSCRIBE BTC\n")
        expect(receive_lines(subscriber, 1), ["Subscribed"])

        trader.sendall(
            b"ADD 1 100 5 BUY LIMIT BTC\n"   # rests
            b"ADD 2 100 5 SELL LIMIT BTC\n"  # crosses -> 1 trade
        )

        expect(receive_lines(trader, 2), ["Accepted", "Accepted"])

        # TRADE <instrument> <seq> <buyerId> <sellerId> <price> <qty>
        expect(
            receive_lines(subscriber, 1),
            ["EVENT TRADE BTC 0 1 2 100 5"]
        )


def test_event_fanout(port):
    subscribers = [connect(port) for _ in range(3)]

    try:
        for sub in subscribers:
            sub.sendall(b"SUBSCRIBE BTC\n")

        for sub in subscribers:
            expect(receive_lines(sub, 1), ["Subscribed"])

        with connect(port) as trader:
            trader.sendall(
                b"ADD 1 100 5 BUY LIMIT BTC\n"
                b"ADD 2 100 5 SELL LIMIT BTC\n"
            )
            receive_lines(trader, 2)

        for sub in subscribers:
            expect(
                receive_lines(sub, 1),
                ["EVENT TRADE BTC 0 1 2 100 5"]
            )
    finally:
        for sub in subscribers:
            sub.close()


def test_instrument_filtering(port):
    # ETH subscriber must not see BTC events; channel proven live by a
    # subsequent ETH trade arriving normally.
    with connect(port) as subscriber, connect(port) as trader:
        subscriber.sendall(b"SUBSCRIBE ETH\n")
        expect(receive_lines(subscriber, 1), ["Subscribed"])

        trader.sendall(
            b"ADD 1 100 5 BUY LIMIT BTC\n"
            b"ADD 2 100 5 SELL LIMIT BTC\n"
        )
        receive_lines(trader, 2)

        expect_silence(subscriber)

        trader.sendall(
            b"ADD 3 200 5 BUY LIMIT ETH\n"
            b"ADD 4 200 5 SELL LIMIT ETH\n"
        )
        receive_lines(trader, 2)

        expect(
            receive_lines(subscriber, 1),
            ["EVENT TRADE ETH 1 3 4 200 5"]  # seq is engine-global: 0 was BTC
        )


def test_events_only_after_subscribe(port):
    # Trades that happened before SUBSCRIBE are not replayed; only new
    # trades generate events.
    with connect(port) as subscriber, connect(port) as trader:
        trader.sendall(
            b"ADD 1 100 5 BUY LIMIT BTC\n"
            b"ADD 2 100 5 SELL LIMIT BTC\n"  # trade seq 0, pre-subscribe
        )
        receive_lines(trader, 2)

        subscriber.sendall(b"SUBSCRIBE BTC\n")
        expect(receive_lines(subscriber, 1), ["Subscribed"])

        trader.sendall(
            b"ADD 3 100 5 BUY LIMIT BTC\n"
            b"ADD 4 100 5 SELL LIMIT BTC\n"  # trade seq 1
        )
        receive_lines(trader, 2)

        expect(
            receive_lines(subscriber, 1),
            ["EVENT TRADE BTC 1 3 4 100 5"]
        )


def test_unsubscribe_stops_events(port):
    with connect(port) as subscriber, connect(port) as trader:
        subscriber.sendall(b"SUBSCRIBE BTC\n")
        expect(receive_lines(subscriber, 1), ["Subscribed"])

        subscriber.sendall(b"UNSUBSCRIBE BTC\n")
        expect(receive_lines(subscriber, 1), ["Unsubscribed"])

        trader.sendall(
            b"ADD 1 100 5 BUY LIMIT BTC\n"
            b"ADD 2 100 5 SELL LIMIT BTC\n"
        )
        receive_lines(trader, 2)

        expect_silence(subscriber)

        # And the connection still works for normal commands.
        subscriber.sendall(b"TRADES\n")
        expect(receive_lines(subscriber, 1), ["1"])


def test_publisher_receives_own_events(port):
    # A self-subscribed trader gets its verdict and the events in order.
    with connect(port) as client:
        client.sendall(b"SUBSCRIBE BTC\n")
        expect(receive_lines(client, 1), ["Subscribed"])

        client.sendall(
            b"ADD 1 100 5 BUY LIMIT BTC\n"
            b"ADD 2 100 5 SELL LIMIT BTC\n"
        )

        expect(
            receive_lines(client, 3),
            [
                "Accepted",
                "Accepted",
                "EVENT TRADE BTC 0 1 2 100 5",
            ]
        )


def test_multi_trade_sweep_events(port):
    # One incoming order sweeping two price levels emits one EVENT per
    # fill, in sequence order.
    with connect(port) as subscriber, connect(port) as trader:
        subscriber.sendall(b"SUBSCRIBE BTC\n")
        expect(receive_lines(subscriber, 1), ["Subscribed"])

        trader.sendall(
            b"ADD 10 100 2 BUY LIMIT BTC\n"
            b"ADD 11 105 3 BUY LIMIT BTC\n"
            b"ADD 12 100 10 SELL LIMIT BTC\n"  # sweeps 105 then 100
        )

        expect(
            receive_lines(trader, 3),
            ["Accepted", "Accepted", "Accepted"]
        )

        expect(
            receive_lines(subscriber, 2),
            [
                "EVENT TRADE BTC 0 11 12 105 3",
                "EVENT TRADE BTC 1 10 12 100 2",
            ]
        )


def test_partial_fill_event_quantity(port):
    # Event quantity is the filled amount, not the order size.
    with connect(port) as subscriber, connect(port) as trader:
        subscriber.sendall(b"SUBSCRIBE BTC\n")
        expect(receive_lines(subscriber, 1), ["Subscribed"])

        trader.sendall(
            b"ADD 1 100 10 BUY LIMIT BTC\n"
            b"ADD 2 100 4 SELL LIMIT BTC\n"
        )
        receive_lines(trader, 2)

        expect(
            receive_lines(subscriber, 1),
            ["EVENT TRADE BTC 0 1 2 100 4"]
        )


def test_multi_instrument_subscriber(port):
    with connect(port) as subscriber, connect(port) as trader:
        subscriber.sendall(b"SUBSCRIBE BTC\nSUBSCRIBE ETH\n")
        expect(
            receive_lines(subscriber, 2),
            ["Subscribed", "Subscribed"]
        )

        trader.sendall(
            b"ADD 1 100 5 BUY LIMIT BTC\n"
            b"ADD 2 100 5 SELL LIMIT BTC\n"
            b"ADD 3 200 5 BUY LIMIT ETH\n"
            b"ADD 4 200 5 SELL LIMIT ETH\n"
        )
        receive_lines(trader, 4)

        expect(
            receive_lines(subscriber, 2),
            [
                "EVENT TRADE BTC 0 1 2 100 5",
                "EVENT TRADE ETH 1 3 4 200 5",
            ]
        )


def test_unsubscribing_one_keeps_other(port):
    with connect(port) as subscriber, connect(port) as trader:
        subscriber.sendall(
            b"SUBSCRIBE BTC\n"
            b"SUBSCRIBE ETH\n"
            b"UNSUBSCRIBE BTC\n"
        )
        expect(
            receive_lines(subscriber, 3),
            ["Subscribed", "Subscribed", "Unsubscribed"]
        )

        trader.sendall(
            b"ADD 1 100 5 BUY LIMIT BTC\n"
            b"ADD 2 100 5 SELL LIMIT BTC\n"
        )
        receive_lines(trader, 2)

        expect_silence(subscriber)

        trader.sendall(
            b"ADD 3 200 5 BUY LIMIT ETH\n"
            b"ADD 4 200 5 SELL LIMIT ETH\n"
        )
        receive_lines(trader, 2)

        expect(
            receive_lines(subscriber, 1),
            ["EVENT TRADE ETH 1 3 4 200 5"]
        )


# ---------------------------------------------------------------------
# Lifecycle: disconnect cleanup, churn, and the slow-consumer drop.
# ---------------------------------------------------------------------

def test_disconnect_cleans_subscription(port):
    with connect(port) as observer, connect(port) as trader:
        observer.sendall(b"SUBSCRIBE BTC\n")
        expect(receive_lines(observer, 1), ["Subscribed"])

        # A subscriber vanishes without unsubscribing.
        leaver = connect(port)
        leaver.sendall(b"SUBSCRIBE BTC\n")
        receive_lines(leaver, 1)
        leaver.close()

        time.sleep(0.1)

        # Broadcasting to the stale fd must not hurt anyone else.
        trader.sendall(
            b"ADD 1 100 5 BUY LIMIT BTC\n"
            b"ADD 2 100 5 SELL LIMIT BTC\n"
        )
        receive_lines(trader, 2)

        expect(
            receive_lines(observer, 1),
            ["EVENT TRADE BTC 0 1 2 100 5"]
        )

        # A fresh subscriber on the same instrument works normally.
        with connect(port) as newcomer:
            newcomer.sendall(b"SUBSCRIBE BTC\n")
            expect(receive_lines(newcomer, 1), ["Subscribed"])

            trader.sendall(
                b"ADD 3 100 5 BUY LIMIT BTC\n"
                b"ADD 4 100 5 SELL LIMIT BTC\n"
            )
            receive_lines(trader, 2)

            expect(
                receive_lines(newcomer, 1),
                ["EVENT TRADE BTC 1 3 4 100 5"]
            )


def test_subscribe_does_not_affect_order_channel(port):
    # Subscription is orthogonal to trading: commands keep working in
    # both directions on the same connection.
    with connect(port) as client:
        client.sendall(
            b"SUBSCRIBE BTC\n"
            b"ADD 1 100 5 BUY LIMIT BTC\n"
            b"GET 1\n"
            b"CANCEL 1\n"
            b"GET 1\n"
            b"UNSUBSCRIBE BTC\n"
        )

        expect(
            receive_lines(client, 6),
            [
                "Subscribed",
                "Accepted",
                "1 100 5 Buy Limit BTC",
                "Cancelled",
                "Not Found",
                "Unsubscribed",
            ]
        )


failures = []

run_test("subscribe_unsubscribe_replies", test_subscribe_unsubscribe_replies)
run_test("subscribe_arg_validation", test_subscribe_arg_validation)
run_test("basic_event_delivery", test_basic_event_delivery)
run_test("event_fanout", test_event_fanout)
run_test("instrument_filtering", test_instrument_filtering)
run_test("events_only_after_subscribe", test_events_only_after_subscribe)
run_test("unsubscribe_stops_events", test_unsubscribe_stops_events)
run_test("publisher_receives_own_events", test_publisher_receives_own_events)
run_test("multi_trade_sweep_events", test_multi_trade_sweep_events)
run_test("partial_fill_event_quantity", test_partial_fill_event_quantity)
run_test("multi_instrument_subscriber", test_multi_instrument_subscriber)
run_test("unsubscribing_one_keeps_other", test_unsubscribing_one_keeps_other)
run_test("disconnect_cleans_subscription", test_disconnect_cleans_subscription)
run_test(
    "subscribe_does_not_affect_order_channel",
    test_subscribe_does_not_affect_order_channel
)

if failures:
    print(f"\n{len(failures)} test(s) failed: {', '.join(failures)}")
    sys.exit(1)

print("Market data tests passed")
