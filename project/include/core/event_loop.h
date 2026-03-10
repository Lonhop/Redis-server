#ifndef REDIS_CORE_EVENT_LOOP_H
#define REDIS_CORE_EVENT_LOOP_H

#include <chrono>
#include <functional>
#include <memory>
#include <cstdint>

namespace redis::core {

    // Режимы работы сокета
    enum class SocketMode : uint8_t {
        NONE = 0,
        READ = 1 << 0,
        WRITE = 1 << 1,
        READ_WRITE = READ | WRITE // (1 << 2) - 1
    };

    inline constexpr SocketMode operator|(SocketMode a, SocketMode b) {
        return static_cast<SocketMode>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline constexpr bool operator&(SocketMode a, SocketMode b) {
        return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
    }

    // Типы событий
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
    // Результат операции
    enum class EventResult : int32_t {
        SUCCESS = 0,
        TIMEOUT = -1,
        ERROR = -2,
        ABORTED = -3
    };
    // Структура события для IOCP
    struct EventData {
        EventType type = EventType::NONE;
        int fd = -1;
        uintptr_t completion_key = 0;
        DWORD bytes_transferred = 0;
        DWORD error_code = 0;
        void* user_data = nullptr;
        bool is_socket = true;
    };
    // Callback для обработки событий
    using EventHandler = std::function<void(const EventData& event)>;
    using TimerHandler = std::function<void(uint64_t timer_id)>;


    class EventLoop {
    public:
        virtual ~EventLoop() = default;

        // Добавление сокета в цикл
        virtual bool addSocket(int fd, SocketMode mode, uintptr_t completion_key = 0) noexcept = 0;
        // Обновление режима сокета
        virtual bool updateSocket(int fd, SocketMode mode) noexcept = 0;
        // Удаление сокета из цикла
        virtual bool removeSocket(int fd) noexcept = 0;

        // Асинхронное чтение
        virtual bool asyncRead(int fd, void* buffer, DWORD size, uintptr_t completion_key = 0) noexcept = 0;
        // Асинхронная запись
        virtual bool asyncWrite(int fd, const void* buffer, DWORD size, uintptr_t completion_key = 0) noexcept = 0;
        // Асинхронный accept
        virtual bool asyncAccept(int listen_fd, int* accept_fd, sockaddr* addr, socklen_t* addrlen, uintptr_t completion_key = 0) noexcept = 0;
        // Асинхронный connect
        virtual bool asyncConnect(int fd, const sockaddr* addr, socklen_t addrlen,uintptr_t completion_key = 0) noexcept = 0;

        // Создание таймера
        virtual uint64_t createTimer(std::chrono::milliseconds interval,bool periodic = false) noexcept = 0;
        // Отмена таймера
        virtual bool cancelTimer(uint64_t timer_id) noexcept = 0;

        // Отправка пользовательского события
        virtual bool postUserEvent(uintptr_t completion_key, void* user_data = nullptr) noexcept = 0;
        // Однократный опрос событий
        virtual EventResult pollOnce(Duration timeout, EventHandler handler) noexcept = 0;
        // Бесконечный цикл обработки
        virtual void run(EventHandler handler) noexcept = 0;
        // Остановка цикла (асинхронная)
        virtual void stop() noexcept = 0;
        // Проверка, запущен ли цикл
        virtual bool isRunning() const noexcept = 0;
        // Количество активных сокетов
        virtual size_t activeSockets() const noexcept = 0;
        // Количество активных таймеров
        virtual size_t activeTimers() const noexcept = 0;
        // Получение последней ошибки
        virtual DWORD lastError() const noexcept = 0;
        // Сброс ошибки
        virtual void resetError() noexcept = 0;
    };
    enum class LoopType {
        IOCP,           // Windows IOCP
        SELECT,         // fallback для совместимости
        DEFAULT = IOCP
    };
    // Создание event loop
    std::unique_ptr<EventLoop> createEventLoop(LoopType type = LoopType::DEFAULT);

}

#endif