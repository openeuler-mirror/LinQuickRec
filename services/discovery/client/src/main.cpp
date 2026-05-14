#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <gflags/gflags.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "common/logger.h"
#include "registry_backend.h"

DEFINE_string(service_type, "",
    "Service type in snake_case (e.g. proxy, feature_service)");
DEFINE_int32(service_port, 0,
    "Main service listening port");
DEFINE_string(registry_backend, "discovery_server",
    "Registry backend: discovery_server or etcd");
DEFINE_string(discovery_addr, "127.0.0.1:8100",
    "Discovery server address");
DEFINE_string(etcd_endpoints, "127.0.0.1:2379",
    "etcd endpoints, comma-separated (for etcd backend)");
DEFINE_string(host, "auto",
    "Container IP (auto = auto-detect)");
DEFINE_int32(heartbeat_interval, 5,
    "Heartbeat interval (seconds)");
DEFINE_int32(health_check_timeout, 2,
    "TCP health check timeout (seconds)");
DEFINE_int32(fail_threshold, 3,
    "Consecutive failure threshold before deregister");
DEFINE_int32(startup_timeout, 30,
    "Max seconds to wait for service port to be ready");

static std::atomic<bool> g_running{true};
static std::string g_instance_id;
static bool g_registered = false;

static void signal_handler(int signum) {
    LOG_INFO << "Received signal " << signum << ", shutting down";
    g_running = false;
}

static bool check_health(const std::string& host, int port, int timeout_sec) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    struct timeval tv = {timeout_sec, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
               (const char*)&tv, sizeof(tv));

    bool ok = (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    closesocket(sock);
    WSACleanup();
    return ok;
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    struct timeval tv = {timeout_sec, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    bool ok = (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    close(sock);
    return ok;
#endif
}

static std::string detect_host() {
    if (FLAGS_host != "auto") {
        return FLAGS_host;
    }

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints;
        struct addrinfo* res = nullptr;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(hostname, nullptr, &hints, &res) == 0) {
            struct sockaddr_in* addr =
                reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
            freeaddrinfo(res);
            return ip;
        }
    }

    return "127.0.0.1";
}

int main(int argc, char* argv[]) {
    {
        common::logger::LoggerConfig cfg;
        cfg.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%f:%L] %v";
        common::logger::Initialize(cfg);
    }

    google::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_service_type.empty() || FLAGS_service_port == 0) {
        LOG_ERROR << "Error: --service_type and --service_port are required";
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::string host = detect_host();

    std::string backend_addr = (FLAGS_registry_backend == "etcd")
        ? FLAGS_etcd_endpoints : FLAGS_discovery_addr;

    LOG_INFO << "Discovery Client starting";
    LOG_INFO << "  backend: " << FLAGS_registry_backend;
    LOG_INFO << "  address: " << backend_addr;
    LOG_INFO << "  service_type: " << FLAGS_service_type;
    LOG_INFO << "  service_port: " << FLAGS_service_port;
    LOG_INFO << "  host: " << host;
    LOG_INFO << "  heartbeat_interval: " << FLAGS_heartbeat_interval << "s";
    LOG_INFO << "  fail_threshold: " << FLAGS_fail_threshold;
    LOG_INFO << "  startup_timeout: " << FLAGS_startup_timeout << "s";

    auto backend = discovery::CreateRegistryBackend(
        FLAGS_registry_backend, backend_addr);

    int waited = 0;
    while (waited < FLAGS_startup_timeout) {
        if (check_health("127.0.0.1", FLAGS_service_port,
                         FLAGS_health_check_timeout)) {
            LOG_INFO << "Main service port " << FLAGS_service_port
                      << " is ready";
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        waited++;
    }
    if (waited >= FLAGS_startup_timeout) {
        LOG_WARN << "Main service port " << FLAGS_service_port
                      << " not ready after " << FLAGS_startup_timeout
                      << "s, proceeding anyway";
    }

    int fail_count = 0;

    while (g_running) {
        bool healthy = check_health("127.0.0.1", FLAGS_service_port,
                                    FLAGS_health_check_timeout);

        if (healthy) {
            fail_count = 0;

            if (!g_registered) {
                auto result = backend->Register(
                    FLAGS_service_type, host, FLAGS_service_port,
                    FLAGS_heartbeat_interval);

                if (result.success) {
                    g_instance_id = result.instance_id;
                    g_registered = true;
                    LOG_INFO << "Registered as " << g_instance_id;
                }
            } else {
                auto result = backend->Heartbeat(
                    FLAGS_service_type, g_instance_id);

                if (result.needs_reregister) {
                    LOG_WARN << "Server lost state, re-registering";
                    g_registered = false;
                } else if (!result.success) {
                    LOG_WARN << "Heartbeat had issues";
                }
            }
        } else {
            fail_count++;
            LOG_WARN << "Health check failed (" << fail_count
                         << "/" << FLAGS_fail_threshold << ")";

            if (g_registered && fail_count >= FLAGS_fail_threshold) {
                backend->Deregister(FLAGS_service_type, g_instance_id);
                LOG_INFO << "Deregistered due to health check failure";
                g_registered = false;
            }
        }

        for (int i = 0; i < FLAGS_heartbeat_interval && g_running; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    if (g_registered) {
        LOG_INFO << "Shutting down, deregistering...";
        if (backend->Deregister(FLAGS_service_type, g_instance_id)) {
            LOG_INFO << "Deregistered successfully on shutdown";
        } else {
            LOG_ERROR << "Deregister on shutdown failed";
        }
    }

    LOG_INFO << "Discovery Client stopped";
    return 0;
}
