#include "core/connection.h"
#include "core/serialization.h"
#include "core/commands.h"
#include "utils/logger.h"
#include "utils/raii_helpers.h"
#include <unistd.h>
#include <sys/socket.h>
#include <system_error>

namespace redis::core {
    Connection::Connection(int fd, EventLoop& loop, ThreadPool& tp, data_structures::HashMap& db, utils::TimerQueue& timers) : fd_(fd), loop_(loop), threadPool_(tp), db_(db), timerQueue_(timers), deadlineTimer_() {
        readBuf_.resize(4096);
        state_ = State::READ;
        refreshIdleTimer();
        LOG_DEBUG("Connection created");
    }
    Connection::~Connection() {
        close();
    }
    void Connection::start() {
        if (!loop_.addSocket(fd_, SocketMode::READ, reinterpret_cast<uintptr_t>(this))) {
            LOG_ERROR("Failed to add socket");
            close();
        }
    }
    void Connection::close() {
        if (state_ == State::CLOSED) return;
        utils::ScopedFlag closingFlag(stateClosed_ ? &stateClosed_ : nullptr, true);
        state_ = State::CLOSED;
        loop_.removeSocket(fd_);
        ::close(fd_);
        cancelIdleTimer();
        LOG_DEBUG("Connection closed");
    }
    void Connection::refreshIdleTimer() {
        cancelIdleTimer();
        idleTimerId_ = loop_.createTimer(redis::config::K_IDLE_TIMEOUT_MS, false);
        if (idleTimerId_ == 0) {
            LOG_WARN("Failed to create idle timer");
        }
    }
    void Connection::cancelIdleTimer() {
        if (idleTimerId_ != 0) {
            loop_.cancelTimer(idleTimerId_);
            idleTimerId_ = 0;
        }
    }
    void Connection::onReadable() {
        if (state_ == State::CLOSED) return;
        refreshIdleTimer();
        ssize_t n = read(fd_, readBuf_.data() + readEnd_, readBuf_.size() - readEnd_);
        if (n <= 0) {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                close();
            }
            return;
        }
        readEnd_ += n;
        processInput();
    }
    void Connection::processInput() {
        while (state_ == State::READ) {
            std::vector<std::string> cmd;
            if (!tryParseRequest(cmd)) break;
            std::vector<char> out;
            executeCommand(cmd, out);
            sendResponse(std::move(out));
        }
    }
    bool Connection::tryParseRequest(std::vector<std::string>& cmd) {
        if (readEnd_ - readPos_ < 4) return false;
        uint32_t len = 0;
        std::memcpy(&len, readBuf_.data() + readPos_, 4);
        if (len > K_MAX_MSG) {
            LOG_ERROR("Request too long");
            throw std::runtime_error("Request too long");
        }
        if (readEnd_ - readPos_ < 4 + len) return false;
        if (!parse_req(reinterpret_cast<const uint8_t*>(readBuf_.data() + readPos_ + 4), len, cmd)) {
            LOG_ERROR("Bad request format");
            throw std::runtime_error("Bad request format");
        }
        readPos_ += 4 + len;
        if (readPos_ == readEnd_) {
            readPos_ = readEnd_ = 0;
        }
        else if (readPos_ > readBuf_.size() / 2) {
            std::memmove(readBuf_.data(), readBuf_.data() + readPos_, readEnd_ - readPos_);
            readEnd_ -= readPos_;
            readPos_ = 0;
        }
        return true;
    }
    void Connection::executeCommand(const std::vector<std::string>& cmd, std::vector<char>& out) {
        utils::ScopedCounter statsCounter(threadPool_.getStats().active_commands);
        try {
            auto& factory = CommandFactory::instance();
            auto command = factory.create(cmd[0], db_, threadPool_, timerQueue_);
            if (!command) {
                serialization::encode_error(out, ErrorCode::UNKNOWN_CMD, "Unknown command");
                return;
            }
            command->execute(cmd, out);
        }
        catch (const std::exception& e) {
            LOG_ERROR("Command execution failed");
            serialization::encode_error(out, ErrorCode::INTERNAL, "Internal server error");
        }
    }
    void Connection::sendResponse(std::vector<char>&& data) {
        if (state_ == State::CLOSED) return;
        if (writeBuf_.empty()) {
            ssize_t n = write(fd_, data.data(), data.size());
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    writeBuf_ = std::move(data);
                    writeSent_ = 0;
                    loop_.updateSocket(fd_, SocketMode::READ | SocketMode::WRITE);
                }
                else {
                    close();
                }
            }
            else if (static_cast<size_t>(n) < data.size()) {
                writeBuf_ = std::move(data);
                writeSent_ = n;
                loop_.updateSocket(fd_, SocketMode::READ | SocketMode::WRITE);
            }
        }
        else {
            writeBuf_.insert(writeBuf_.end(), data.begin(), data.end());
            loop_.updateSocket(fd_, SocketMode::READ | SocketMode::WRITE);
        }
    }
    void Connection::onWritable() {
        if (state_ == State::CLOSED) return;
        refreshIdleTimer();
        if (writeBuf_.empty()) {
            loop_.updateSocket(fd_, SocketMode::READ);
            return;
        }
        ssize_t n = write(fd_, writeBuf_.data() + writeSent_, writeBuf_.size() - writeSent_);
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                close();
            }
            return;
        }
        writeSent_ += n;
        if (writeSent_ == writeBuf_.size()) {
            writeBuf_.clear();
            writeSent_ = 0;
            loop_.updateSocket(fd_, SocketMode::READ);
        }
    }
    void Connection::onTimer(uint64_t timer_id) {
        if (timer_id == idleTimerId_) {
            LOG_INFO("Connection timeout");
            close();
        }
    }
}