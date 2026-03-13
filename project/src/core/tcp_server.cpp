#include "core/tcp_server.h"
#include "utils/logger.h"
#include <cstring>
#include <system_error>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

namespace redis::core {
    TcpServer::TcpServer(EventLoop& loop, uint16_t port, ConnectionFactory factory) : loop_(loop), listen_fd_(-1), factory_(std::move(factory)) {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            throw std::system_error(errno, std::generic_category(), "socket");
        }
        int flags = fcntl(listen_fd_, F_GETFL, 0);
        if (flags < 0) {
            ::close(listen_fd_);
            throw std::system_error(errno, std::generic_category(), "F_GETFL");
        }
        if (fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
            ::close(listen_fd_);
            throw std::system_error(errno, std::generic_category(), "F_SETFL");
        }
        int opt = 1;
        if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            ::close(listen_fd_);
            throw std::system_error(errno, std::generic_category(), "SO_REUSEADDR");
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            ::close(listen_fd_);
            throw std::system_error(errno, std::generic_category(), "bind");
        }
        if (listen(listen_fd_, SOMAXCONN) < 0) {
            ::close(listen_fd_);
            throw std::system_error(errno, std::generic_category(), "listen");
        }
        LOG_INFO("Server listening");
    }
    TcpServer::~TcpServer() {
        stop();
    }
    void TcpServer::start() {
        if (!stopped_) return;
        stopped_ = false;
        if (!loop_.addSocket(listen_fd_, SocketMode::READ,
                            reinterpret_cast<uintptr_t>(this))) {
            LOG_ERROR("Failed to add listen socket to event loop");
            return;
                            }
        LOG_DEBUG("TCP server started");
    }
    void TcpServer::stop() {
        if (stopped_) return;
        stopped_ = true;
        loop_.removeSocket(listen_fd_);
        connections_.clear();
        LOG_DEBUG("TCP server stopped");
    }
    void TcpServer::on_accept(EventData event) {
        if (stopped_) return;
        if (event.error_code != 0) {
            LOG_ERROR("Accept error: " + std::to_string(event.error_code));
            return;
        }
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (sockaddr*)&client_addr, &addrlen);
        if (client_fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_ERROR("accept failed");
            }
            return;
        }
        int flags = fcntl(client_fd, F_GETFL, 0);
        if (flags < 0) {
            LOG_ERROR("F_GETFL failed for client socket");
            ::close(client_fd);
            return;
        }
        if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            LOG_ERROR("F_SETFL failed for client socket");
            ::close(client_fd);
            return;
        }
        auto conn = factory_(client_fd);
        if (!conn) {
            LOG_ERROR("Connection factory returned null");
            ::close(client_fd);
            return;
        }
        connections_.push_back(std::move(conn));
        connections_.back()->start();

        LOG_DEBUG("New connection accepted");
    }
    void TcpServer::remove_connection(Connection* conn) {
        auto it = std::find_if(connections_.begin(), connections_.end(), [conn](const auto& ptr) { return ptr.get() == conn; });
        if (it != connections_.end()) {
            connections_.erase(it);
            LOG_DEBUG("Connection removed");
        }
    }
}