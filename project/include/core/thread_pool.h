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
}

#endif