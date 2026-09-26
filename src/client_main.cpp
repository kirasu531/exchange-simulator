#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>

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

bool SendUint32(int fd, uint32_t value){
    value = htonl(value);

    return SendAll(fd, &value, sizeof(value));
}

bool RecvUint32(int fd, uint32_t &value){
    uint32_t network_value;

    if ( !RecvAll(fd, &network_value, sizeof(network_value)) ){
        return false;
    }

    value = ntohl(network_value);

    return true;
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

    while ( true ){
        uint32_t receive_value;

        if ( !RecvUint32(client_fd, receive_value) ){
            std::cout << "Server closed\n";
            
            break;
        }

        std::cout << "Value received: " << receive_value << '\n';

        std::cout << "Enter a value to send: ";

        uint32_t value;
        std::cin >> value;

        SendUint32(client_fd, value);
    }

    close(client_fd);
}