#ifndef REDIS_SERVER_TCP_SERVER_H
#define REDIS_SERVER_TCP_SERVER_H

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
    private:
        void on_accept(EventData event);
        void remove_connection(Connection* conn);
        EventLoop& loop_;
        int listen_fd_;
        ConnectionFactory factory_;
        std::vector<std::unique_ptr<Connection>> connections_;
        bool stopped_ = false;
    };
}
#endif