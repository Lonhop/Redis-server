#include "core/connection.h"
#include "utils/logger.h"
#include "utils/raii_helpers.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace {
    std::vector<std::string> resp_to_args(const std::string& resp) {
        std::vector<std::string> result;
        size_t pos = 0;
        if (resp.empty() || resp[0] != '*') return result;
        pos = 1;
        size_t n = std::stoul(resp.substr(pos));
        pos = resp.find("\r\n", pos) + 2;
        for (size_t i = 0; i < n; ++i) {
            if (pos >= resp.size()) break;
            if (resp[pos] != '$') break;
            pos++;
            size_t len = std::stoul(resp.substr(pos));
            pos = resp.find("\r\n", pos) + 2;
            result.emplace_back(resp.substr(pos, len));
            pos += len + 2;
        }
        return result;
    }
}

namespace redis::core {
    constexpr size_t K_MAX_MSG = 1024 * 1024;
    constexpr auto K_IDLE_TIMEOUT = std::chrono::seconds(60);
    Connection::Connection(int fd, EventLoop& loop, KeyValueStore& store)
        : fd_(fd), loop_(loop), store_(store) {
        readBuf_.resize(4096);
        refreshIdleTimer();
        LOG_DEBUG("Connection created");
    }
    Connection::~Connection() {
        close();
    }
    void Connection::start() {
        if (!loop_.addSocket(fd_, SocketMode::READ, reinterpret_cast<uintptr_t>(this))) {
            LOG_ERROR("Failed to add socket to event loop");
            close();
        }
    }
    void Connection::close() {
        if (closed_) return;
        closed_ = true;
        loop_.removeSocket(fd_);
        if (fd_ >= 0) {
            closesocket(static_cast<SOCKET>(fd_));
            fd_ = -1;
        }
        cancelIdleTimer();
        LOG_DEBUG("Connection closed");
    }
    void Connection::refreshIdleTimer() {
        cancelIdleTimer();
        idleTimerId_ = loop_.createTimer(K_IDLE_TIMEOUT, false);
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
        if (closed_) return;
        refreshIdleTimer();
        int n = recv(static_cast<SOCKET>(fd_), readBuf_.data() + readEnd_, static_cast<int>(readBuf_.size() - readEnd_), 0);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                LOG_ERROR("recv failed");
                close();
            }
            return;
        }
        if (n == 0) {
            close();
            return;
        }
        readEnd_ += n;
        processInput();
    }
    void Connection::processInput() {
        while (!closed_) {
            std::vector<std::string> cmd;
            if (!tryParseRequest(cmd)) break;
            std::vector<char> out;
            executeCommand(cmd, out);
            sendResponse(std::move(out));
        }
    }
    bool Connection::tryParseRequest(std::vector<std::string>& cmd) {
        std::string input(readBuf_.data() + readPos_, readEnd_ - readPos_);
        size_t pos = input.find("\r\n");
        if (pos == std::string::npos) return false;
        if (input[0] != '*') {
            LOG_ERROR("Invalid RESP format");
            close();
            return false;
        }
        size_t msg_len = input.find("\r\n", pos + 2);
        if (msg_len == std::string::npos) return false;
        msg_len += 2;
        cmd = resp_to_args(input.substr(0, msg_len));
        if (cmd.empty()) {
            LOG_ERROR("Failed to parse command");
            close();
            return false;
        }
        readPos_ += msg_len;
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
        try {
            auto command = create_command(cmd[0], store_);
            if (!command) {
                out = RespSerializer::error("ERR unknown command").str();
                return;
            }
            RespValue result = command->execute(cmd);
            out = result.serialize();
        } catch (const std::exception& e) {
            LOG_ERROR("Command execution failed");
            out = RespSerializer::error("ERR internal error").str();
        }
    }
    void Connection::sendResponse(std::vector<char>&& data) {
        if (closed_) return;
        if (writeBuf_.empty()) {
            int n = send(static_cast<SOCKET>(fd_), data.data(), static_cast<int>(data.size()), 0);
            if (n == SOCKET_ERROR) {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    writeBuf_ = std::move(data);
                    writeSent_ = 0;
                    loop_.updateSocket(fd_, SocketMode::READ | SocketMode::WRITE);
                }
                else {
                    LOG_ERROR("send failed");
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
        if (closed_) return;
        refreshIdleTimer();
        if (writeBuf_.empty()) {
            loop_.updateSocket(fd_, SocketMode::READ);
            return;
        }
        int n = send(static_cast<SOCKET>(fd_),
                     writeBuf_.data() + writeSent_,
                     static_cast<int>(writeBuf_.size() - writeSent_),
                     0);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                LOG_ERROR("send failed");
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