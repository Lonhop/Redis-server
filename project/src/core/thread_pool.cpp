#include "core/thread_pool.h"
#include <algorithm>
#include <chrono>

namespace redis::core {
    ThreadPool::ThreadPool(size_t numThreads) {
        if (numThreads == 0) {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 2;
        }
        config_.min_threads = numThreads;
        config_.max_threads = numThreads;
        min_threads_ = numThreads;
        max_threads_ = numThreads;
        init(numThreads);
    }
    ThreadPool::ThreadPool(const ThreadPoolConfig& config) : config_(config), min_threads_(config.min_threads), max_threads_(config.max_threads) {
        size_t numThreads = config.min_threads;
        if (numThreads == 0) {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 2;
        }
        init(numThreads);
    }
    ThreadPool::ThreadPool(ThreadPool&& other) noexcept
        : workers_(std::move(other.workers_))
        , tasks_(std::move(other.tasks_))
        , high_priority_tasks_(std::move(other.high_priority_tasks_))
        , stop_(other.stop_.load())
        , paused_(other.paused_.load())
        , next_task_id_(other.next_task_id_.load())
        , completed_tasks_(other.completed_tasks_.load())
        , failed_tasks_(other.failed_tasks_.load())
        , active_thread_count_(other.active_thread_count_.load())
        , config_(std::move(other.config_))
        , min_threads_(other.min_threads_)
        , max_threads_(other.max_threads_)
        , error_handler_(std::move(other.error_handler_))
        , threads_to_stop_(other.threads_to_stop_.load()) {
    }
    ThreadPool& ThreadPool::operator=(ThreadPool&& other) noexcept {
        if (this != &other) {
            shutdown();
            workers_ = std::move(other.workers_);
            tasks_ = std::move(other.tasks_);
            high_priority_tasks_ = std::move(other.high_priority_tasks_);
            stop_.store(other.stop_.load());
            paused_.store(other.paused_.load());
            next_task_id_.store(other.next_task_id_.load());
            completed_tasks_.store(other.completed_tasks_.load());
            failed_tasks_.store(other.failed_tasks_.load());
            active_thread_count_.store(other.active_thread_count_.load());
            config_ = std::move(other.config_);
            min_threads_ = other.min_threads_;
            max_threads_ = other.max_threads_;
            error_handler_ = std::move(other.error_handler_);
            threads_to_stop_.store(other.threads_to_stop_.load());
        }
        return *this;
    }
    ThreadPool::~ThreadPool() {
        shutdown();
    }
    void ThreadPool::init(size_t numThreads) {
        workers_.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            ThreadContext ctx;
            ctx.thread = std::thread(&ThreadPool::worker, this, &ctx);
            workers_.push_back(std::move(ctx));
        }
    }
    void ThreadPool::shutdown() {
        stop_.store(true);
        cv_.notify_all();
        for (auto& ctx : workers_) {
            if (ctx.thread.joinable()) {
                ctx.thread.join();
            }
        }
        workers_.clear();
    }
    void ThreadPool::worker(ThreadContext* ctx) {
        ctx->active.store(true);
        active_thread_count_.fetch_add(1);
        while (!stop_.load()) {
            if (threads_to_stop_.load() > 0) {
                size_t expected = threads_to_stop_.load();
                if (threads_to_stop_.compare_exchange_strong(expected, expected - 1)) {
                    break;
                }
            }
            Task task;
            bool have_task = false;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] {return stop_.load() || (!paused_.load() && (!high_priority_tasks_.empty() || !tasks_.empty())); });
                if (stop_.load()) break;
                if (!high_priority_tasks_.empty()) {
                    task = std::move(high_priority_tasks_.front());
                    high_priority_tasks_.pop_front();
                    have_task = true;
                } else if (!tasks_.empty()) {
                    task = std::move(tasks_.front());
                    tasks_.pop_front();
                    have_task = true;
                }
            }
            if (have_task) {
                try {
                    task.func();
                    completed_tasks_.fetch_add(1);
                }
                catch (...) {
                    failed_tasks_.fetch_add(1);
                    if (error_handler_) {
                        error_handler_(std::current_exception());
                    }
                }
                ctx->task_processed.fetch_add(1);
            }
            if (config_.enable_stealing && !stop_.load() && !paused_.load()) {
                Task stolen;
                if (stealTask(stolen)) {
                    try {
                        stolen.func();
                        completed_tasks_.fetch_add(1);
                    }
                    catch (...) {
                        failed_tasks_.fetch_add(1);
                        if (error_handler_) {
                            error_handler_(std::current_exception());
                        }
                    }
                    ctx->task_processed.fetch_add(1);
                }
            }
        }
        ctx->active.store(false);
        active_thread_count_.fetch_sub(1);
    }
    bool ThreadPool::stealTask(Task& task) {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        if (!high_priority_tasks_.empty()) {
            task = std::move(high_priority_tasks_.front());
            high_priority_tasks_.pop_front();
            return true;
        }
        if (!tasks_.empty()) {
            task = std::move(tasks_.front());
            tasks_.pop_front();
            return true;
        }
        return false;
    }
    void ThreadPool::enqueue(std::function<void()> task) {
        if (stop_.load()) {
            throw std::runtime_error("Can't enqueue'");
        }
        if (config_.queue_size_limit > 0) {
            size_t current_size;
            {
                std::unique_lock lock(mutex_);
                current_size = tasks_.size() + high_priority_tasks_.size();
            }
            if (current_size >= config_.queue_size_limit) {
                throw std::runtime_error("Queue is full");
            }
        }
        Task t{
            .func = std::move(task),
            .priority = TaskPriority::NORMAL,
            .id = next_task_id_.fetch_add(1),
            .enqueue_time = std::chrono::steady_clock::now()
        };
        {
            std::unique_lock lock(mutex_);
            tasks_.push_back(std::move(t));
        }

        cv_.notify_one();
    }
    void ThreadPool::enqueue(std::function<void()> task, TaskPriority priority) {
        if (stop_.load()) {
            throw std::runtime_error("Can't enqueue");
        }

        if (config_.queue_size_limit > 0) {
            size_t current_size;
            {
                std::unique_lock lock(mutex_);
                current_size = tasks_.size() + high_priority_tasks_.size();
            }
            if (current_size >= config_.queue_size_limit) {
                throw std::runtime_error("Queue is full");
            }
        }
        Task t{
            .func = std::move(task),
            .priority = priority,
            .id = next_task_id_.fetch_add(1),
            .enqueue_time = std::chrono::steady_clock::now()
        };
        {
            std::unique_lock lock(mutex_);
            if (priority >= TaskPriority::HIGH) {
                high_priority_tasks_.push_back(std::move(t));
            } else {
                tasks_.push_back(std::move(t));
            }
        }
        cv_.notify_one();
    }
    bool ThreadPool::resize(size_t new_size) {
        if (new_size < min_threads_ || new_size > max_threads_) {
            return false;
        }
        size_t current = workers_.size();
        if (new_size > current) {
            for (size_t i = current; i < new_size; ++i) {
                ThreadContext ctx;
                ctx.thread = std::thread(&ThreadPool::worker, this, &ctx);
                workers_.push_back(std::move(ctx));
            }
        }
        else if (new_size < current) {
            size_t threads_to_remove = current - new_size;
            threads_to_stop_.store(threads_to_remove);
            cv_.notify_all();
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            while (threads_to_stop_.load() > 0 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            auto it = std::remove_if(workers_.begin(), workers_.end(),[](const ThreadContext& ctx) { return !ctx.active.load(); });
            if (it != workers_.end()) {
                for (auto iter = it; iter != workers_.end(); ++iter) {
                    if (iter->thread.joinable()) {
                        iter->thread.join();
                    }
                }
                workers_.erase(it, workers_.end());
            }
        }
        return true;
    }
    void ThreadPool::pause() {
        paused_.store(true);
    }
    void ThreadPool::resume() {
        paused_.store(false);
        cv_.notify_all();
    }
    std::vector<std::function<void()>> ThreadPool::clear() {
        std::vector<std::function<void()>> pending;
        {
            std::unique_lock lock(mutex_);
            while (!high_priority_tasks_.empty()) {
                pending.push_back(std::move(high_priority_tasks_.front().func));
                high_priority_tasks_.pop_front();
            }
            while (!tasks_.empty()) {
                pending.push_back(std::move(tasks_.front().func));
                tasks_.pop_front();
            }
        }
        return pending;
    }
    void ThreadPool::waitAll() {
        std::unique_lock lock(mutex_);
        wait_cv_.wait(lock, [this] { return tasks_.empty() && high_priority_tasks_.empty() && active_thread_count_.load() == 0;});
    }
    bool ThreadPool::waitAllFor(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return wait_cv_.wait_for(lock, timeout, [this] {
            return tasks_.empty() && high_priority_tasks_.empty() && active_thread_count_.load() == 0;
        });
    }
    size_t ThreadPool::activeThreads() const noexcept {
        return active_thread_count_.load();
    }
    ThreadPoolStats ThreadPool::getStats() const {
        ThreadPoolStats stats;
        stats.active_threads = active_thread_count_.load();
        stats.idle_threads = workers_.size() - stats.active_threads;
        stats.pending_tasks = tasks_.size() + high_priority_tasks_.size();
        stats.total_tasks_completed = completed_tasks_.load();
        stats.total_tasks_failed = failed_tasks_.load();
        if (!tasks_.empty() || !high_priority_tasks_.empty()) {
            auto now = std::chrono::steady_clock::now();
            uint64_t total_wait = 0;
            size_t count = 0;
            {
                std::unique_lock lock(mutex_);
                for (const auto& task : high_priority_tasks_) {
                    auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(now - task.enqueue_time);
                    total_wait += wait.count();
                    ++count;
                }
                for (const auto& task : tasks_) {
                    auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(now - task.enqueue_time);
                    total_wait += wait.count();
                    ++count;
                }
            }
            if (count > 0) {
                stats.avg_wait_time = std::chrono::milliseconds(total_wait / count);
            }
        }
        return stats;
    }
    void ThreadPool::setErrorHandler(std::function<void(std::exception_ptr)> handler) {
        error_handler_ = std::move(handler);
    }
    void ThreadPool::cleanup() {
        stop_.store(true);
        cv_.notify_all();
        for (auto& ctx : workers_) {
            if (ctx.thread.joinable()) {
                ctx.thread.join();
            }
        }
        workers_.clear();
        tasks_.clear();
        high_priority_tasks_.clear();
        threads_to_stop_.store(0);
    }
    void ThreadPool::notifyAll() {
        cv_.notify_all();
    }
}