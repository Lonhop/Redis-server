#pragma once

#include "event_loop.h"

#ifdef _WIN32 // Проверка на windows
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>

namespace redis::core {
    // Типы операций
    enum class OpType{
        NONE = 0,
        ACCEPT = 1,
        READ = 2,
        WRITE = 3,
        CONNECT = 4,
        TIMER = 5,
        USER = 6
    };

    // Расширенная структура для IOCP
    struct OverlappedEx : public OVERLAPPED // from minwinbase.h встроена в windows kits
    {
        OpType op = OpType::NONE;   // тип операции
        int fd = -1;    // файловый дескриптор
        uintptr_t completion_key = 0;   // пользовательский ключ
        WSABUF wbuf{};  // буфер для WSASend/WSARecv
        char buffer[8192];  // встроенный буфер
        sockaddr_storage addr{};    // для accept/connect
        int addr_len = 0;   // длина адреса
        int* accept_fd_ptr = nullptr;   // указатель на новый сокет для accept
        EventType event_type = EventType::NONE; // тип события для callback

        OverlappedEx() {
            Internal = 0;
            InternalHigh = 0;
            Offset = 0;
            OffsetHigh = 0;
            hEvent = nullptr;
        }
    };


    // Контекст сокета для iocp
    struct SocketContext {
        SOCKET fd = INVALID_SOCKET;
        SocketMode mode = SocketMode::NONE;
        uintptr_t completion_key = 0;
        bool pending_read = false;
        bool pending_write = false;
        bool pending_accept = false;
        bool pending connect = false;
        std::vector<std::unique_ptr<OverlappedEx>> active_ops;
        std::mutex ops_mutex;
        explicit SocketContext(SOCKET f) : fd(f) {}
    };

    class IocpPoler : public EventLoop {
    public:
        IocpPoller();
        explicit IocpPoller(const EventLoopConfig& config);
        ~IocpPoller() override;

        // Запрет копирования
        IocpPoller(IocpPoller&) = delete;
        IocpPoller& operator=(const IocpPoller&) = delete;
        // Перемещение
        IocpPoller(IocpPoller&& other) noexcept;
        IocpPoller& operator=(const IocpPoller&& other) noexcept;

        // Управление сокетами
        bool addSocket(int fd, SocketMode mode, uintptr_t completion_key = 0) noexcept override;
        bool updateSocket(int fd, SocketMode mode) noexcept override;
        bool removeSocket(int fd) noexcept override;
        SocketMode getSocketMode(int fd) const noexcept override;

        // Асинхронные операции
        bool asyncRead(int fd, void* buffer, DWORD size, uintptr_t completion_key = 0) noexcept override;
        bool asyncWrite(int fd, const void* buffer, DWORD size, uintptr_t completion_key = 0) noexcept override;
        bool asyncAccept(int listen_fd, int* accept_fd, sockaddr* addr, socklen_t addrlen, uintptr_t completion_key = 0) noexcept override;
        bool asyncConnect(int fd, const sockaddr* addr, socklen_t addrlen, uintptr_t completion_key = 0) noexcept override;
        bool ca ncelIO(int fd) noexcept override;

        // Таймеры
        uint64_t createTimer(Duration interval, bool periodic = false) noexcept override;
        bool cancelTimer(uint64_t timer_id) noexcept override;

        // Пользовательские события
        bool postUserEvent(uintptr_t completion_key, void* user_data = nullptr) noexcept override;

        // Запуск цикла
        EventResult pollOnce(Duration timeout, EventHandler handler) noexcept override;
        void run(EventHandler handler) noexcept override;
        void stop() noexcept override;

        // Состояния
        bool isRunning() const noexcept override { return running_.load(); }
        size_t activeSockets() const noexcept override { return sockets_.size(); }
        size_t activeTimers() const noexcept override { return timers_.size(); }
        uint64_t totalEvents() const noexcept override { return total_events_.load(); }

        // Обработка ошибок
        DWORD lastError() const noexcept override { return last_error_; }
        void resetError() noexcept override { last_error_ = 0; }

    private:
        HANDLE iocp_;                                      // IOCP handle
        HANDLE timer_queue_;                               // очередь таймеров
        std::atomic<bool> running_{false};                 // флаг работы
        std::atomic<uint64_t> total_events_{0};            // счетчик событий
        mutable DWORD last_error_ = 0;                      // последняя ошибка

        bool associateSocket(SOCKET fd, uintptr_t completion_key) noexcept;
        void startAsyncAccept(SocketContext* ctx, OverlappedEx* ov);
        bool startAsyncRead(SocketContext* ctx, OverlappedEx* ov, void* buffer, DWORD size);
        bool startAsyncWrite(SocketContext* ctx, OverlappedEx* ov, const void* buffer, DWORD size);
        void processCompletion(OVERLAPPED* ov, DWORD bytes, DWORD error, EventHandler handler);
        void cleanupSocket(SocketContext* ctx);
        void checkTimeouts();

        // Хранилища
        std::unordered_map<int, std::unique_ptr<SocketContext>> sockets_;
        std::unordered_map<uint64_t, HANDLE> timers_;
        std::vector<std::unique_ptr<OverlappedEx>> overlapped_pool_;

        // Сихронизация
        mutable std::shared_mutex sockets_mutex_
        mutable std::mutex timers_mutex_;
        std::mutex pool_mutex_;

        // Конфигурация
        EventLoopConfig config_;
        size_t max_worker_threads_ = 0;

        // Статистика
        uint64_t total_operations_ = 0;
        uint64_t failed_operations_ = 0;

        // Функции для получения указателей на функции AcceptEx/ConnectEx
        LPFN_ACCEPTEX lpfnAcceptEx = nullptr;
        LPFN_CONNECTEX lpfnConnectEx = nullptr;

        bool loadExtensionFunctions(SOCKET fd) noexcept;
    };
}