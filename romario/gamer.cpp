#include <iostream>
#include "client.h"

//-------------------------------------------------------------------------------------------------

IOClient::IOClient()
{
    sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ < 0) {
        perror("Failed to create socket\n");
        throw;
    }
}

IOClient::~IOClient()
{
    close(sockfd_);
}

bool IOClient::Connection(size_t port) const
{
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // INADDR_ANY [in production]
    if (connect(sockfd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Connection failed");
        return false;
    }
    return true;
}

int IOClient::SendAll(const std::string& buf, int flags) const
{
    size_t total_sent = 0;
    while (total_sent < buf.length()) {
        int current_sent = send(sockfd_, buf.c_str() + total_sent, buf.length() - total_sent, flags);
        if (current_sent == -1)
            return -1;
        total_sent += current_sent;
    }   
    return total_sent;
}

int IOClient::RecvAll(std::string& buf, int flags) const
{
    int current_recv_count = 0;
    std::string buf_length;
    buf_length.resize(4);
    current_recv_count = recv(sockfd_, &buf_length, buf_length.length(), flags);
    if (current_recv_count == -1) {
        return -1;
    }
    size_t length = atoi(buf_length.c_str());
    buf.resize(length);
    std::string current_buf;
    current_buf.resize(1024);
    size_t total_recv_count = 0;
    while (total_recv_count < length) {
        current_recv_count = recv(sockfd_, &current_buf, current_buf.length(), flags);
        if (current_recv_count == -1) {
            return -1;
        }
        total_recv_count += current_recv_count;
        buf += current_buf;
        current_buf.clear();
    }   
    return total_recv_count;
}

//-------------------------------------------------------------------------------------------------

Gamer::Gamer() {}
Gamer::~Gamer() {}

bool Gamer::StartGame(size_t port)
{
    while (!client_.Connection(port)) {
        std::cout << "Connection..." << std::endl;
    }
    std::string start_game_request = "CLI_SUB_REQUEST";
    int send_result = client_.SendAll(start_game_request, 0);
    if (send_result == -1) {
        return false;
    }
    std::string start_game_answer;
    if (client_.RecvAll(start_game_answer, 0) == -1 ||
        start_game_answer == "FAIL")
    {
        return false;
    }
    id_ = start_game_answer;
    return true;
}

//-------------------------------------------------------------------------------------------------

int main(int argc, char const *argv[]) {

    size_t port = 1234;
    Gamer<MovingStrategies> gamer;

    // request to start a game
    while (!gamer.StartGame(port)) {}

    //strategy_.setState();
    


    // get data from server
    //FieldState field_state;



    return 0;
}
