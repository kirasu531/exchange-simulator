// Unit tests for the I/O helpers and per-connection buffer state in
// include/network_utils.hpp.
//
// These tests do not start the server; socketpair() provides real
// sockets so partial sends, EOF, and EAGAIN behave exactly as they do
// for TCP peers, without needing a listening server or loopback ports.

#include <catch2/catch_test_macros.hpp>

#include "network_utils.hpp"

#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include <cerrno>
#include <cstring>
#include <string>


struct SocketPair{
    int fds[2];

    SocketPair(){
        REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    }

    ~SocketPair(){
        close(fds[0]);
        close(fds[1]);
    }
};


void setNonBlocking(int fd){
    int flags = fcntl(fd, F_GETFL);

    REQUIRE(flags != -1);
    REQUIRE(fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1);
}


TEST_CASE("Connection tracks pending output [network_utils]"){
    Connection connection;

    REQUIRE(!connection.hasPendingData());
    REQUIRE(connection.pendingBytes() == 0);
    REQUIRE(connection.getSendData() == nullptr);

    connection.output_buffer = "verdict\n";

    REQUIRE(connection.hasPendingData());
    REQUIRE(connection.pendingBytes() == 8);
    REQUIRE(connection.getSendData() == connection.output_buffer.data());

    connection.sent_offset = 3;

    REQUIRE(connection.pendingBytes() == 5);
    REQUIRE(
        static_cast<const char*>(connection.getSendData())
        == connection.output_buffer.data() + 3
    );

    connection.sent_offset = 8;

    REQUIRE(!connection.hasPendingData());
    REQUIRE(connection.getSendData() == nullptr);
}


TEST_CASE("SendAll and Recv move bytes between peers [network_utils]"){
    SocketPair pair;

    const std::string payload = "ADD 1 100 5 BUY LIMIT BTC\n";

    REQUIRE(
        SendAll(pair.fds[0], payload.data(), payload.size())
        == IOResult::Success
    );

    char buffer[64];
    std::string pending;

    REQUIRE(
        Recv(pair.fds[1], pending, buffer, sizeof(buffer))
        == IOResult::Success
    );
    REQUIRE(pending == payload);
}


TEST_CASE("Recv reports WouldBlock and Closed [network_utils]"){
    SocketPair pair;
    setNonBlocking(pair.fds[1]);

    char buffer[64];
    std::string pending;

    REQUIRE(
        Recv(pair.fds[1], pending, buffer, sizeof(buffer))
        == IOResult::WouldBlock
    );

    REQUIRE(shutdown(pair.fds[0], SHUT_WR) == 0);
    REQUIRE(
        Recv(pair.fds[1], pending, buffer, sizeof(buffer))
        == IOResult::Closed
    );
}


TEST_CASE("Send reports partial progress then WouldBlock [network_utils]"){
    SocketPair pair;
    setNonBlocking(pair.fds[0]);

    const std::string payload(1 << 20, 'x');  // larger than any socket buffer
    size_t sent = 0;

    auto result = Send(
        pair.fds[0],
        payload.data(),
        payload.size(),
        sent
    );

    REQUIRE(result == IOResult::WouldBlock);
    REQUIRE(sent > 0);
    REQUIRE(sent < payload.size());
}


TEST_CASE("WriteAll delivers the full buffer [network_utils]"){
    SocketPair pair;

    const std::string payload = "record one\nrecord two\n";

    REQUIRE(
        WriteAll(pair.fds[0], payload.data(), payload.size())
        == IOResult::Success
    );

    char buffer[64];
    ssize_t received = recv(pair.fds[1], buffer, sizeof(buffer), 0);

    REQUIRE(received == static_cast<ssize_t>(payload.size()));
    REQUIRE(std::string(buffer, received) == payload);
}


TEST_CASE("RecvLine drains a line already inside pending [network_utils]"){
    SocketPair pair;

    std::string pending = "GET 9\nleftover";
    std::string line;
    char buffer[16];

    // The newline is already buffered, so no recv() is needed at all.
    REQUIRE(
        RecvLine(pair.fds[1], pending, line, buffer, sizeof(buffer))
        == RecvLineResult::Success
    );
    REQUIRE(line == "GET 9");
    REQUIRE(pending == "leftover");
}


TEST_CASE("RecvLine assembles a line across recv boundaries [network_utils]"){
    SocketPair pair;

    std::string pending = "par";
    std::string line;
    char buffer[64];

    const std::string payload = "tial line\nrest";

    REQUIRE(
        SendAll(pair.fds[0], payload.data(), payload.size())
        == IOResult::Success
    );
    REQUIRE(
        RecvLine(pair.fds[1], pending, line, buffer, sizeof(buffer))
        == RecvLineResult::Success
    );

    REQUIRE(line == "partial line");
    REQUIRE(pending == "rest");
}


TEST_CASE("RecvLine rejects an oversized line [network_utils]"){
    SocketPair pair;

    std::string pending;
    std::string line;
    char buffer[64];

    // More than MAX_LINE_SIZE bytes without a newline.
    const std::string payload(MAX_LINE_SIZE + 50, 'A');

    REQUIRE(
        SendAll(pair.fds[0], payload.data(), payload.size())
        == IOResult::Success
    );
    REQUIRE(
        RecvLine(pair.fds[1], pending, line, buffer, sizeof(buffer))
        == RecvLineResult::TooLarge
    );
}


TEST_CASE("RecvLine reports peer shutdown [network_utils]"){
    SocketPair pair;

    std::string pending;
    std::string line;
    char buffer[16];

    REQUIRE(shutdown(pair.fds[0], SHUT_WR) == 0);
    REQUIRE(
        RecvLine(pair.fds[1], pending, line, buffer, sizeof(buffer))
        == RecvLineResult::Closed
    );
}
