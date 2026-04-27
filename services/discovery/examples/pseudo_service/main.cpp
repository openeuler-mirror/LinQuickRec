#include <iostream>
#include <csignal>
#include <cstring>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    int port = 8001;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind port " << port << std::endl;
        close(server_fd);
        return 1;
    }

    listen(server_fd, 5);
    std::cout << "Pseudo service listening on port " << port << std::endl;

    fd_set read_fds;
    struct timeval tv;
    while (g_running) {
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(server_fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(server_fd, &read_fds)) {
            struct sockaddr_in client;
            socklen_t client_len = sizeof(client);
            int client_fd = accept(server_fd, (struct sockaddr*)&client, &client_len);
            if (client_fd >= 0) {
                std::cout << "Accepted connection from "
                          << inet_ntoa(client.sin_addr) << ":"
                          << ntohs(client.sin_port) << std::endl;
                const char* response = "OK\n";
                send(client_fd, response, strlen(response), 0);
                close(client_fd);
            }
        }
    }

    close(server_fd);
    std::cout << "Pseudo service on port " << port << " stopped" << std::endl;
    return 0;
}
