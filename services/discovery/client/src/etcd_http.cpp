#include "etcd_http.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <sstream>
#include <stdexcept>

namespace etcd_http {

std::string post(const std::string& host_str,
                 int port,
                 const std::string& path,
                 const std::string& body) {

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    struct addrinfo hints;
    struct addrinfo* res = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host_str.c_str(), nullptr, &hints, &res) != 0) {
        close(sock);
        return "";
    }

    struct sockaddr_in* addr_in =
        reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
    addr.sin_addr = addr_in->sin_addr;
    freeaddrinfo(res);

    struct timeval tv = {3, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(sock);
        return "";
    }

    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host_str << ":" << port << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;

    std::string req_str = req.str();
    size_t sent = 0;
    while (sent < req_str.size()) {
        ssize_t n = send(sock, req_str.c_str() + sent,
                         req_str.size() - sent, 0);
        if (n <= 0) { close(sock); return ""; }
        sent += static_cast<size_t>(n);
    }

    std::string response;
    char buf[4096];
    while (true) {
        ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        response += buf;
    }
    close(sock);

    size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) return "";
    return response.substr(header_end + 4);
}

} // namespace etcd_http
