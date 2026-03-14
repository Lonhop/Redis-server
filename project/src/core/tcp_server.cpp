#include "core/tcp_server.h"
#include "utils/logger.h"
#include <cstring>
#include <system_error>

namespace redis::core {
    class WinsockInit {
    public:
        WinsockInit() {
            WSADATA wsaData;
            int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (err != 0) {
                throw std::system_error(err, std::system_category(), "WSAStartup");
            }
        }
        ~WinsockInit() {
            WSACleanup();
        }
    };
    static WinsockInit g_winsockInit;
    TcpServer::TcpServer(EventLoop& loop, uint16_t port, ConnectionFactory factory) : loop_(loop), listen_fd_(INVALID_SOCKET), factory_(std::move(factory)), stopped_(true) {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ == INVALID_SOCKET) {
            int err = WSAGetLastError();
            throw std::system_error(err, std::system_category(), "socket");
        }
        u_long mode = 1;
        if (ioctlsocket(listen_fd_, FIONBIO, &mode) != 0) {
            int err = WSAGetLastError();
            closesocket(listen_fd_);
            throw std::system_error(err, std::system_category(), "ioctlsocket");
        }
        BOOL opt = TRUE;
        if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
                       reinterpret_cast<char*>(&opt), sizeof(opt)) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            closesocket(listen_fd_);
            throw std::system_error(err, std::system_category(), "setsockopt SO_REUSEADDR");
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            closesocket(listen_fd_);
            throw std::system_error(err, std::system_category(), "bind");
        }
        if (listen(listen_fd_, SOMAXCONN) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            closesocket(listen_fd_);
            throw std::system_error(err, std::system_category(), "listen");
        }
        LOG_INFO("TCP server listening on port");
    }
    TcpServer::~TcpServer() {
        stop();
        if (listen_fd_ != INVALID_SOCKET) {
            closesocket(listen_fd_);
            listen_fd_ = INVALID_SOCKET;
        }
    }
    void TcpServer::start() {
        if (!stopped_) return;
        stopped_ = false;
        if (!loop_.addSocket(static_cast<int>(listen_fd_), SocketMode::READ, reinterpret_cast<uintptr_t>(this))) {
            LOG_ERROR("Failed to add listen socket to event loop");
            stop();
        }
        LOG_DEBUG("TCP server started");
    }
    void TcpServer::stop() {
        if (stopped_) return;
        stopped_ = true;
        loop_.removeSocket(static_cast<int>(listen_fd_));
        connections_.clear();
        LOG_DEBUG("TCP server stopped");
    }
    void TcpServer::handleAccept(EventData event) {
        if (stopped_) return;
        if (event.error_code != 0) {
            LOG_ERROR("Accept error: " + std::to_string(event.error_code));
            return;
        }
        sockaddr_in client_addr{};
        int addrlen = sizeof(client_addr);
        SOCKET client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addrlen);
        if (client_fd == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                LOG_ERROR("accept failed");
            }
            return;
        }
        u_long mode = 1;
        if (ioctlsocket(client_fd, FIONBIO, &mode) != 0) {
            LOG_ERROR("ioctlsocket failed for client socket");
            closesocket(client_fd);
            return;
        }
        auto conn = factory_(static_cast<int>(client_fd));
        if (!conn) {
            LOG_ERROR("Connection factory returned null");
            closesocket(client_fd);
            return;
        }
        connections_.push_back(std::move(conn));
        connections_.back()->start();
        LOG_DEBUG("New connection accepted");
    }
    void TcpServer::remove_connection(Connection* conn) {
        auto it = std::find_if(connections_.begin(), connections_.end(),
            [conn](const auto& ptr) { return ptr.get() == conn; });
        if (it != connections_.end()) {
            connections_.erase(it);
            LOG_DEBUG("Connection removed");
        }
    }
}