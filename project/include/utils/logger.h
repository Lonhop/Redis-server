#ifndef REDIS_SERVER_LOGGER_H
#define REDIS_SERVER_LOGGER_H

#pragma once

#include <string>
#include <string_view>
#include <source_location>
#include <mutex>
#include <atomic>
#include <fstream>
#include <memory>
#include <chrono>

namespace redis::utils {
    // Настраиваемый уровень логирования
    enum class LogLevel {
        DEBUG = 0,
        INFO = 1,
        WARN = 2,
        ERROR = 3
    };

    // Конфигурация логгера
    struct LoggerConfig {
        LogLevel min_level = LogLevel::INFO;        // минимальный уровень для вывода
        bool log_to_console = true;                  // вывод в консоль
        bool log_to_file = false;                     // вывод в файл
        std::string filename = "server.log";         // имя файла лога
        bool async = false;                            // асинхронный режим
        size_t max_file_size = 100 * 1024 * 1024;    // 100 MB
        size_t max_files = 5;                          // максимальное число файлов при ротации
        bool include_timestamp = true;                 // включать временную метку
        bool include_location = true;                   // включать информацию о месте вызова
    };

    // Основной класс логгера Singleton
    class Logger {
    public:
        using Level = LogLevel;
        static Logger& instance(); // Получение экземпляра логгера

        // Запрет копирования
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        // Конфигурация логгера
        void configure(const LoggerConfig& cfg);

        // Установка минимального уровня логирования
        void set_level(Level level);
        Level level() const { return level_.load(std::memory_order_relaxed); } // получение текущего уровня

        // Логирование с форматированием
        template<typename... Args>
        void debug(std::format_string<Args...> fmt, Args&&... args,std::source_location loc = std::source_location::current()) {
            log(Level::DEBUG, std::format(fmt, std::forward<Args>(args)...), loc);
        }

        template<typename... Args>
        void info(std::format_string<Args...> fmt, Args&&... args,std::source_location loc = std::source_location::current()) {
            log(Level::INFO, std::format(fmt, std::forward<Args>(args)...), loc);
        }

        template<typename... Args>
        void warn(std::format_string<Args...> fmt, Args&&... args,std::source_location loc = std::source_location::current()) {
            log(Level::WARN, std::format(fmt, std::forward<Args>(args)...), loc);
        }

        template<typename... Args>
        void error(std::format_string<Args...> fmt, Args&&... args,std::source_location loc = std::source_location::current()) {
            log(Level::ERROR, std::format(fmt, std::forward<Args>(args)...), loc);
        }

        void flush(); // Принудительный сброс

    private:
        Logger();
        ~Logger();

        struct LogEntry {
            Level level;
            std::string message;
            std::chrono::system_clock::time_point timestamp;
            std::source_location location;
        };

        void log(Level level, const std::string& message, std::source_location loc);
        void log_impl(const LogEntry& entry);
        void rotate_files();
        std::string level_to_string(Level level) const;
        std::string format_entry(const LogEntry& entry) const;
        void open_log_file();

        // Конфигурация
        LoggerConfig config_;
        std::atomic<Level> level_{Level::INFO};

        // Синхронизация
        mutable std::mutex mutex_;
        mutable std::mutex file_mutex_;

        // Файловый вывод
        std::unique_ptr<std::ofstream> file_;
        size_t current_file_size_ = 0;
    };

    // Макросы для удобства
    #define LOG_DEBUG(fmt, ...) redis::utils::Logger::instance().debug(fmt, ##__VA_ARGS__)
    #define LOG_INFO(fmt, ...)  redis::utils::Logger::instance().info(fmt, ##__VA_ARGS__)
    #define LOG_WARN(fmt, ...)  redis::utils::Logger::instance().warn(fmt, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) redis::utils::Logger::instance().error(fmt, ##__VA_ARGS__)

}
#endif