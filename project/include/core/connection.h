#ifndef REDIS_SERVER_CONNECTION_H
#define REDIS_SERVER_CONNECTION_H
#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include "core/event_loop.h"
#include "core/thread_pool.h"
#include "data_structures/hash_map.h"
#include "utils/timer.h"

namespace redis::core {

    class Connection : public std::enable_shared_from_this<Connection> {
    public:
        using Ptr = std::shared_ptr<Connection>;
        Connection(int fd, EventLoop& loop, ThreadPool& tp, data_structures::HashMap& db, utils::TimerQueue& timers);
        ~Connection();
        void start();
        void close();
        bool isClosed() const { return state_ == State::CLOSED; }
        int fd() const { return fd_; }
        void onReadable();
        void onWritable();
        void onTimer(uint64_t timer_id);
    private:
        int fd_;
        EventLoop& loop_;
        ThreadPool& threadPool_;
        data_structures::HashMap& db_;
        utils::TimerQueue& timerQueue_;
        State state_ = State::READ;
        bool stateClosed_ = false;
        std::vector<char> readBuf_;
        size_t readPos_ = 0;
        size_t readEnd_ = 0;
        std::vector<char> writeBuf_;
        size_t writeSent_ = 0;
        uint64_t idleTimerId_ = 0;
        utils::DeadlineTimer deadlineTimer_;
        enum class State { READ, CLOSED };
        void refreshIdleTimer();
        void cancelIdleTimer();
        void processInput();
        bool tryParseRequest(std::vector<std::string>& cmd);
        void executeCommand(const std::vector<std::string>& cmd, std::vector<char>& out);
        void sendResponse(std::vector<char>&& data);
    };
}
#endif