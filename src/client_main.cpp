#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <cassert>

bool SendAll(int fd, const void *data, size_t size){
    auto bytes = static_cast <const char*> (data);

    size_t sent = 0;

    while ( sent < size ){
        ssize_t result = send(
            fd,
            bytes + sent,
            size - sent,
            MSG_NOSIGNAL
        );

        if ( result <= 0 ) return false;

        sent += result;
    }
    
    return true;
}

int RecvAll(int fd, void *data, size_t size){
    auto bytes = static_cast <char*> (data);

    int received = 0;

    while ( received < size ){
        ssize_t result = recv(
            fd,
            bytes + received,
            size - received,
            0
        );

        if ( result <= 0 ) break;

        received += result;
    }

    return received;
}

bool RecvLine(int fd, std::string &pending, std::string &line, void *data, size_t size){
    line.clear();

    {
        bool found = false;
        int idx = -1;

        for ( int i = 0; size_t(i) < pending.size(); i++ ){
            if ( pending[i] == '\n' ){
                found = true;
                idx = i;
                
                break;
            }
        }

        if ( found ){
            std::string next;

            for ( int i = 0; size_t(i) < pending.size(); i++ ){
                if ( i < idx ) line.push_back(pending[i]);
                if ( i > idx ) next.push_back(pending[i]);
            }

            std::swap(pending, next);

            return true;
        }
    }
    
    auto bytes = static_cast <char*> (data);

    int total_received = 0;

    while ( true ){
        ssize_t bytes_received = recv(fd, bytes, size, 0);

        if ( bytes_received <= 0 ) return false;

        total_received += bytes_received;

        for ( int i = 0; i < bytes_received; i++ ){
            if ( *(bytes + i) == '\n' ){
                line += pending;
                pending.clear();

                for ( int j = i + 1; j < bytes_received; j++ ){
                    pending.push_back(*(bytes + j));
                }

                return true;
            }

            pending.push_back(*(bytes + i));
        }

        if ( total_received > 1000 ) return false;
    }

    assert(false);
}

int main(int argc, char *argv[]){
    int server_port;

    if ( argc < 2 ){
        server_port = 4000;
    } else{
        server_port = std::stoi(argv[1]);
    }

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);

    if ( client_fd < 0 ){
        perror("socket");
        return 0;
    }

    sockaddr_in address;

    address.sin_family = AF_INET;
    address.sin_port = htons(server_port);
    address.sin_addr.s_addr = inet_addr("127.0.0.1");

    int connect_verdict = connect(
        client_fd, 
        reinterpret_cast <const sockaddr*> (&address), 
        sizeof(address)
    );

    if ( connect_verdict == -1 ){
        perror("connect");
        close(client_fd);

        return 0;
    }

    std::cout << "Connection successful\n";

    std::string pending, line;
    char data[100];

    std::string command;

    while (std::getline(std::cin, command)){
        command += '\n';

        assert(SendAll(client_fd, command.data(), command.size()));

        assert(RecvLine(client_fd, pending, line, data, sizeof(data)));

        std::cout << line << '\n';
    }

    close(client_fd);
}