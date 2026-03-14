#ifndef REDIS_CORE_IOCP_POLLER_H
#define REDIS_CORE_IOCP_POLLER_H

#include "event_loop.h"
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <shared_mutex>
#include <chrono>

namespace redis::core {
    enum class OpType : uint8_t {
        NONE = 0,
        ACCEPT = 1,
        READ = 2,
        WRITE = 3,
        CONNECT = 4,
        TIMER = 5,
        USER = 6
    };
    struct OverlappedEx : public OVERLAPPED {
        OpType op = OpType::NONE;
        int fd = -1;
        uintptr_t completion_key = 0;
        WSABUF wbuf{};
        char buffer[8192];
        sockaddr_storage addr{};
        int addr_len = 0;
        int* accept_fd_ptr = nullptr;
        EventType event_type = EventType::NONE;
        std::chrono::steady_clock::time_point start_time{};
        OverlappedEx() {
            Internal = 0;
            InternalHigh = 0;
            Offset = 0;
            OffsetHigh = 0;
            hEvent = nullptr;
        }
    };
    struct SocketContext {
        SOCKET fd = INVALID_SOCKET;
        SocketMode mode = SocketMode::NONE;
        uintptr_t completion_key = 0;
        bool pending_read = false;
        bool pending_write = false;
        bool pending_accept = false;
        bool pending_connect = false;
        std::vector<std::unique_ptr<OverlappedEx>> active_ops;
        std::mutex ops_mutex;
        explicit SocketContext(SOCKET f) : fd(f) {}
    };
    class IocpPoller : public EventLoop {
    public:
        IocpPoller();
        explicit IocpPoller(const EventLoopConfig& config);
        ~IocpPoller() override;
        IocpPoller(const IocpPoller&) = delete;
        IocpPoller& operator=(const IocpPoller&) = delete;
        IocpPoller(IocpPoller&& other) noexcept;
        IocpPoller& operator=(IocpPoller&& other) noexcept;
        bool addSocket(int fd, SocketMode mode, uintptr_t completion_key = 0) noexcept override;
        bool updateSocket(int fd, SocketMode mode) noexcept override;
        bool removeSocket(int fd) noexcept override;
        SocketMode getSocketMode(int fd) const noexcept override;
        bool cancelIO(int fd) noexcept override;
        bool asyncRead(int fd, void* buffer, DWORD size, uintptr_t completion_key = 0) noexcept override;
        bool asyncWrite(int fd, const void* buffer, DWORD size, uintptr_t completion_key = 0) noexcept override;
        bool asyncAccept(int listen_fd, int* accept_fd, sockaddr* addr, int* addrlen, uintptr_t completion_key = 0) noexcept override;
        bool asyncConnect(int fd, const sockaddr* addr, int addrlen, uintptr_t completion_key = 0) noexcept override;
        uint64_t createTimer(Duration interval, bool periodic = false) noexcept override;
        bool cancelTimer(uint64_t timer_id) noexcept override;
        bool postUserEvent(uintptr_t completion_key, void* user_data = nullptr) noexcept override;
        EventResult pollOnce(Duration timeout, EventHandler handler) noexcept override;
        void run(EventHandler handler) noexcept override;
        void stop() noexcept override;
        bool isRunning() const noexcept override { return running_.load(); }
        size_t activeSockets() const noexcept override { return sockets_.size(); }
        size_t activeTimers() const noexcept override { return timers_.size(); }
        uint64_t totalEvents() const noexcept override { return total_events_.load(); }
        DWORD lastError() const noexcept override { return last_error_; }
        void resetError() noexcept override { last_error_ = 0; }
    private:
        HANDLE iocp_;
        HANDLE timer_queue_;
        std::atomic<bool> running_{false};
        std::atomic<uint64_t> total_events_{0};
        mutable DWORD last_error_ = 0;

        bool associateSocket(SOCKET fd, uintptr_t completion_key) noexcept;
        void startAsyncAccept(SocketContext* ctx, OverlappedEx* ov);
        bool startAsyncRead(SocketContext* ctx, OverlappedEx* ov, void* buffer, DWORD size);
        bool startAsyncWrite(SocketContext* ctx, OverlappedEx* ov, const void* buffer, DWORD size);
        void processCompletion(OVERLAPPED* ov, DWORD bytes, DWORD error, EventHandler handler);
        void cleanupSocket(SocketContext* ctx);
        void checkTimeouts();

        std::unordered_map<int, std::unique_ptr<SocketContext>> sockets_;
        std::unordered_map<uint64_t, HANDLE> timers_;
        std::vector<std::unique_ptr<OverlappedEx>> overlapped_pool_;

        mutable std::shared_mutex sockets_mutex_;
        mutable std::mutex timers_mutex_;
        std::mutex pool_mutex_;

        EventLoopConfig config_;
        size_t max_worker_threads_ = 0;

        uint64_t total_operations_ = 0;
        uint64_t failed_operations_ = 0;
        LPFN_ACCEPTEX lpfnAcceptEx = nullptr;
        LPFN_CONNECTEX lpfnConnectEx = nullptr;
        bool loadExtensionFunctions(SOCKET fd) noexcept;
    };

}

#endif