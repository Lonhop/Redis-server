#ifndef REDIS_CORE_TCP_SERVER_H
#define REDIS_CORE_TCP_SERVER_H


#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include "core/event_loop.h"
#include "core/connection.h"

namespace redis::core {

    class TcpServer {
    public:
        using ConnectionFactory = std::function<std::unique_ptr<Connection>(int fd)>;
        TcpServer(EventLoop& loop, uint16_t port, ConnectionFactory factory);
        ~TcpServer();
        void start();
        void stop();
        size_t connection_count() const { return connections_.size(); }
        void handleAccept(EventData event);

    private:
        void remove_connection(Connection* conn);
        EventLoop& loop_;
        SOCKET listen_fd_;
        ConnectionFactory factory_;
        std::vector<std::unique_ptr<Connection>> connections_;
        bool stopped_ = true;
    };
}
#endif