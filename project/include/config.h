#ifndef REDIS_SERVER_CONFIG_H
#define REDIS_SERVER_CONFIG_H

#include <cstdint>
#include <chrono>
#include <limits>
#include <string>

namespace redis {
    // Версия протокола
    constexpr uint32_t PROTOCOL_VERSION = 1;

    // Сетевые константы
    constexpr size_t K_MAX_MSG = 1024 * 1024;  // 1MB
    static_assert(K_MAX_MSG >= 64, "K_MAX_MSG too small for protocol header");
    static_assert(K_MAX_MSG <= 1024 * 1024 * 1024, "K_MAX_MSG > 1GB - DoS risk");

    // Максимальное число аргументов команды
    constexpr size_t K_MAX_ARGS = 1024;
    constexpr size_t K_MAX_TOTAL_ARGS_SIZE = 64 * 1024 * 1024;  // 64MB суммарно
    static_assert(K_MAX_ARGS >= 1, "K_MAX_ARGS must be at least 1");
    static_assert(K_MAX_ARGS <= 10000, "K_MAX_ARGS > 10000 - DoS risk");

    // Таймауты
    constexpr auto K_IDLE_TIMEOUT = std::chrono::milliseconds(5000);   // 5 сек
    constexpr auto K_READ_TIMEOUT = std::chrono::milliseconds(30000);  // 30 сек
    constexpr auto K_WRITE_TIMEOUT = std::chrono::milliseconds(30000); // 30 сек

    // Проверка на минимальные значения и таймауты(медленная загрузка)
    static_assert(K_IDLE_TIMEOUT.count() >= 1000, "Idle timeout too small");
    static_assert(K_READ_TIMEOUT.count() >= 5000, "Read timeout too small");
    static_assert(K_WRITE_TIMEOUT.count() >= 5000, "Write timeout too small");
    static_assert(K_IDLE_TIMEOUT.count() <= 86400000, "Timeout > 1 day");

    // Структуры данных
    // Размер шага при ресайзинге хеш-таблицы
    constexpr size_t K_RESIZING_WORK = 128;
    static_assert(K_RESIZING_WORK >= 16, "Resizing work too small - slows resizing");
    static_assert(K_RESIZING_WORK <= 1024, "Resizing work too large - blocks server");

    // Максимальный load factor
    constexpr double K_MAX_LOAD_FACTOR = 0.75;
    static_assert(K_MAX_LOAD_FACTOR > 0.1 && K_MAX_LOAD_FACTOR < 10.0,"Load factor must be reasonable");

    // Порог для асинхронного удаления
    constexpr size_t K_LARGE_CONTAINER_SIZE = 10000;
    static_assert(K_LARGE_CONTAINER_SIZE <= SIZE_MAX / 2,"Container size threshold too large");

    // Протокол сериализации
    enum class SerialType : uint8_t {
        NIL = 0,
        ERR = 1,
        STR = 2,
        INT = 3,
        ARR = 4,
        MAX = ARR  // для проверки границ
    };

    // Валидация при десериализации
    inline bool is_valid_serial_type(uint8_t val) {
        return val <= static_cast<uint8_t>(SerialType::MAX);
    }

    // Коды ошибок
    enum class ErrorCode : int32_t {
        OK = 0,
        UNKNOWN_CMD = 1,
        TOO_BIG = 2,
        BAD_REQ = 3,
        ARG = 4,
        INTERNAL = 5,      // внутренняя ошибка сервера
        TIMEOUT = 6,       // таймаут операции
        MAX = 6
    };

    // Проверки безопасности
    // Проверка на переполнение при вычислениях
    static_assert(K_MAX_MSG < SIZE_MAX / 2, "K_MAX_MSG too large");
    static_assert(K_MAX_ARGS < SIZE_MAX / sizeof(std::string),"K_MAX_ARGS would cause allocation overflow");
    static_assert(PROTOCOL_VERSION >= 1, "Protocol version must be >= 1"); // Проверка совместимости

}

#endif