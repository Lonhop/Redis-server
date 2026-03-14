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
#include <chrono>

namespace redis::core {

    struct ThreadPoolConfig {
        size_t min_threads = 0;
        size_t max_threads = 0;
        size_t queue_size_limit = 0;
        std::chrono::milliseconds idle_timeout = std::chrono::seconds(60);
        bool enable_stealing = true;
    };

    enum class TaskPriority : uint8_t {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2,
        CRITICAL = 3
    };

    struct ThreadPoolStats {
        size_t active_threads = 0;
        size_t idle_threads = 0;
        size_t pending_tasks = 0;
        size_t total_tasks_completed = 0;
        size_t total_tasks_failed = 0;
        std::chrono::milliseconds avg_wait_time{0};
    };

    class ThreadPool {
    public:
        explicit ThreadPool(size_t numThreads);
        explicit ThreadPool(const ThreadPoolConfig& config);

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        
        //TODO: DEAL WITH THREAD TEST FAILURE
        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;

        ~ThreadPool();

        // enqueue без future
        void enqueue(std::function<void()> task);
        void enqueue(std::function<void()> task, TaskPriority priority);

        // enqueue с future
        template<typename F, typename... Args>
        auto enqueue(F&& f, Args&&... args)
            -> std::future<std::invoke_result_t<F, Args...>>
        {
            using return_type = std::invoke_result_t<F, Args...>;

            auto task =
                std::make_shared<std::packaged_task<return_type()>>(
                    std::bind(
                        std::forward<F>(f),
                        std::forward<Args>(args)...));

            std::future<return_type> result = task->get_future();

            enqueue(std::function<void()>(
                [task]() { (*task)(); }
            ));

            return result;
        }

        template<typename F, typename... Args>
        auto enqueue(TaskPriority priority, F&& f, Args&&... args)
            -> std::future<std::invoke_result_t<F, Args...>>
        {
            using return_type = std::invoke_result_t<F, Args...>;

            auto task =
                std::make_shared<std::packaged_task<return_type()>>(
                    std::bind(
                        std::forward<F>(f),
                        std::forward<Args>(args)...));

            std::future<return_type> result = task->get_future();

            enqueue(
                std::function<void()>(
                    [task]() { (*task)(); }),
                priority);

            return result;
        }

        bool resize(size_t new_size);
        void pause();
        void resume();
        std::vector<std::function<void()>> clear();
        void waitAll();
        bool waitAllFor(std::chrono::milliseconds timeout);

        bool isRunning() const noexcept { return !stop_.load(); }
        bool isPaused() const noexcept { return paused_.load(); }
        size_t pendingTasks() const noexcept {
            std::lock_guard<std::mutex> lock(mutex_);
            return tasks_.size() + high_priority_tasks_.size();
        }
        size_t activeThreads() const noexcept;
        size_t threadCount() const noexcept { return workers_.size(); }

        ThreadPoolStats getStats() const;
        void setErrorHandler(std::function<void(std::exception_ptr)> handler);

    private:
        struct Task {
            std::function<void()> func;
            TaskPriority priority{TaskPriority::NORMAL};
            uint64_t id{0};
            std::chrono::steady_clock::time_point enqueue_time;
            bool operator<(const Task& other) const { return priority < other.priority; }
        };

        struct ThreadContext {
            std::thread thread;
            std::atomic<bool> active{false};
            std::atomic<uint64_t> task_processed{0};

            ThreadContext() = default;
            ThreadContext(ThreadContext&& other) noexcept
                : thread(std::move(other.thread)),
                  active(other.active.load()),
                  task_processed(other.task_processed.load()) {}
            ThreadContext& operator=(ThreadContext&& other) noexcept {
                if (this != &other) {
                    thread = std::move(other.thread);
                    active.store(other.active.load());
                    task_processed.store(other.task_processed.load());
                }
                return *this;
            }
            ThreadContext(const ThreadContext&) = delete;
            ThreadContext& operator=(const ThreadContext&) = delete;
        };

        std::deque<ThreadContext> workers_;               // изменено с vector на deque
        std::deque<Task> tasks_;
        std::deque<Task> high_priority_tasks_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::condition_variable wait_cv_;

        std::atomic<bool> stop_{false};
        std::atomic<bool> paused_{false};
        std::atomic<uint64_t> next_task_id_{0};
        std::atomic<uint64_t> completed_tasks_{0};
        std::atomic<uint64_t> failed_tasks_{0};
        std::atomic<size_t> active_thread_count_{0};

        // Поля, необходимые для точной статистики и уменьшения пула
        std::atomic<size_t> busy_threads_{0};
        std::atomic<size_t> threads_to_stop_{0};

        ThreadPoolConfig config_;
        size_t min_threads_{0};
        size_t max_threads_{0};

        std::function<void(std::exception_ptr)> error_handler_;

        void init(size_t numThreads);
        void worker(ThreadContext* ctx);
        void shutdown();
    };

}

#endif