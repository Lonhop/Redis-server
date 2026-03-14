#ifndef REDIS_CORE_EVENT_LOOP_H
#define REDIS_CORE_EVENT_LOOP_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace redis::core {

    using Duration = std::chrono::milliseconds;

    enum class SocketMode : uint8_t {
        NONE = 0,
        READ = 1 << 0,
        WRITE = 1 << 1,
        READ_WRITE = READ | WRITE
    };

    inline constexpr SocketMode operator|(SocketMode a, SocketMode b) {
        return static_cast<SocketMode>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline constexpr bool operator&(SocketMode a, SocketMode b) {
        return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
    }

    enum class EventType : uint8_t {
        NONE = 0,
        READ = 1,
        WRITE = 2,
        READ_WRITE = 3,
        ACCEPT = 4,
        CONNECT = 5,
        TIMER = 6,
        USER = 7
    };

    enum class EventResult : int32_t {
        SUCCESS = 0,
        TIMEOUT = -1,
        ERROR = -2,
        ABORTED = -3
    };

    struct EventData {
        EventType type = EventType::NONE;
        int fd = -1;
        uintptr_t completion_key = 0;
        DWORD bytes_transferred = 0;
        DWORD error_code = 0;
        void* user_data = nullptr;
        bool is_socket = true;
    };

    using EventHandler = std::function<void(const EventData& event)>;
    using TimerHandler = std::function<void(uint64_t timer_id)>;
    struct EventLoopConfig {
        size_t worker_threads = 0;
        uint32_t operation_timeout = 30000;
    };

    class EventLoop {
    public:
        virtual ~EventLoop() = default;
        virtual bool addSocket(int fd, SocketMode mode, uintptr_t completion_key = 0) noexcept = 0;
        virtual bool updateSocket(int fd, SocketMode mode) noexcept = 0;
        virtual bool removeSocket(int fd) noexcept = 0;
        virtual SocketMode getSocketMode(int fd) const noexcept = 0;
        virtual bool cancelIO(int fd) noexcept = 0;
        virtual bool asyncRead(int fd, void* buffer, DWORD size, uintptr_t completion_key = 0) noexcept = 0;
        virtual bool asyncWrite(int fd, const void* buffer, DWORD size, uintptr_t completion_key = 0) noexcept = 0;
        virtual bool asyncAccept(int listen_fd, int* accept_fd, sockaddr* addr, int* addrlen, uintptr_t completion_key = 0) noexcept = 0;
        virtual bool asyncConnect(int fd, const sockaddr* addr, int addrlen, uintptr_t completion_key = 0) noexcept = 0;
        virtual uint64_t createTimer(Duration interval, bool periodic = false) noexcept = 0;
        virtual bool cancelTimer(uint64_t timer_id) noexcept = 0;
        virtual bool postUserEvent(uintptr_t completion_key, void* user_data = nullptr) noexcept = 0;
        virtual EventResult pollOnce(Duration timeout, EventHandler handler) noexcept = 0;
        virtual void run(EventHandler handler) noexcept = 0;
        virtual void stop() noexcept = 0;
        virtual bool isRunning() const noexcept = 0;
        virtual size_t activeSockets() const noexcept = 0;
        virtual size_t activeTimers() const noexcept = 0;
        virtual uint64_t totalEvents() const noexcept = 0;
        virtual DWORD lastError() const noexcept = 0;
        virtual void resetError() noexcept = 0;
    };
    enum class LoopType {
        IOCP,
        SELECT,
        DEFAULT = IOCP
    };
    std::unique_ptr<EventLoop> createEventLoop(LoopType type = LoopType::DEFAULT);
}
#endif