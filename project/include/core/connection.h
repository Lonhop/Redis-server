#ifndef REDIS_CORE_CONNECTION_H
#define REDIS_CORE_CONNECTION_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <atomic>
#include "core/event_loop.h"
#include "core/commands.h"

namespace redis::core {
    class Connection : public std::enable_shared_from_this<Connection> {
    public:
        using Ptr = std::shared_ptr<Connection>;
        Connection(int fd, EventLoop& loop, KeyValueStore& store);
        ~Connection();
        void start();
        void close();
        bool isClosed() const { return closed_; }
        int fd() const { return fd_; }
        void onReadable();
        void onWritable();
        void onTimer(uint64_t timer_id);
    private:
        int fd_;
        EventLoop& loop_;
        KeyValueStore& store_;
        std::atomic<bool> closed_{false};
        std::vector<char> readBuf_;
        size_t readPos_ = 0;
        size_t readEnd_ = 0;
        std::vector<char> writeBuf_;
        size_t writeSent_ = 0;
        uint64_t idleTimerId_ = 0;
        void processInput();
        bool tryParseRequest(std::vector<std::string>& cmd);
        void executeCommand(const std::vector<std::string>& cmd, std::vector<char>& out);
        void sendResponse(std::vector<char>&& data);
        void refreshIdleTimer();
        void cancelIdleTimer();
    };

}

#endif