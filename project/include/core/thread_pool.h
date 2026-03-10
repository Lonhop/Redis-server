#ifndef REDIS_SERVER_THREAD_POOL_H
#define REDIS_SERVER_THREAD_POOL_H

#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>
#include <type_traits>
#include <stdexcept>

namespace redis::core {

    // Конфигурация пула потоков
    struct ThreadPoolConfig {
        size_t min_threads = 0;              // минимальное число потоков
        size_t max_threads = 0;               // максимальное число потоков
        size_t queue_size_limit = 0;          // лимит очереди
        std::chrono::milliseconds idle_timeout = std::chrono::seconds(60); // таймаут простоя
        bool enable_stealing = true;           // воровство задач
    };
    // Задачи с приоритетом
    enum class TaskPriority : uint8_t {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2,
        CRITICAL = 3
    };
    // Статистика пула
    struct ThreadPoolStats {
        size_t active_threads = 0;  // активно работающих потоков
        size_t idle_threads = 0;    // ожидающих потоков
        size_t pending_tasks = 0;   // задач в очереди
        size_t total_tasks_completed = 0;   // всего выполнено задач
        size_t total_tasks_failed = 0;  // всего задач с ошибками
        std::chrono::milliseconds avg_wait_time{0}; // среднее время ожидания
    };

    class ThreadPool {
    public:
        //  Пул с числом потоков
        explicit ThreadPool(size_t numThreads);
        // Создание пула с конфигурацией
        explicit ThreadPool(const ThreadPoolConfig& config);

        // Запрет Копирования
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        // Перемещение
        ThreadPool(ThreadPool&& other) noexcept;
        ThreadPool& operator=(ThreadPool&& other) noexcept;
        // Деструктор
        ~ThreadPool();

        // Добавление задачи без возвращаемого значения
        void enqueue(std::function<void()> task);
        // Добавление задачи с приоритетом
        void enqueue(std::function<void()> task, TaskPriority priority);
        // Добавление задачи с возвращаемым значением
        template<typename F, typename... Args>
        auto enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>>;
        // Добавление задачи с возвращаемым значением и приоритетом
        template<typename F, typename... Args>
        auto enqueue(TaskPriority priority, F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>>;


        // Динамическое изменение числа потоков
        bool resize(size_t new_size);
        // Приостановка и возобновление обработки
        void pause();
        void resume();
        // Очистка очереди задач
        std::vector<std::function<void()>> clear();

        void waitAll(); // Ожидание завершения всех задач в очереди
        bool waitAllFor(std::chrono::milliseconds timeout); // Ожидание завершения всех задач с таймаутом

        // Проверка состояния
        bool isRunning() const noexcept { return !stop_.load(); }
        bool isPaused() const noexcept { return paused_.load(); }
        size_t pendingTasks() const noexcept { return tasks_.size(); }
        size_t activeThreads() const noexcept;
        size_t threadCount() const noexcept { return workers_.size(); }
        // Получение статистики
        ThreadPoolStats getStats() const;

    };
}

#endif