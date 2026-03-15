#include "core/thread_pool.h"
#include <algorithm>

namespace redis::core {
    ThreadPool::ThreadPool(size_t numThreads) {
        if (numThreads == 0) {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 2;
        }
        min_threads_ = numThreads;
        max_threads_ = std::max(numThreads, size_t(64));
        config_.min_threads = min_threads_;
        config_.max_threads = max_threads_;
        init(numThreads);
    }
    ThreadPool::ThreadPool(const ThreadPoolConfig& config): config_(config), min_threads_(config.min_threads), max_threads_(config.max_threads) {
        size_t numThreads = config.min_threads;
        if (numThreads == 0) {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 2;
        }
        if (max_threads_ == 0) max_threads_ = std::max(numThreads, size_t(64));
        init(numThreads);
    }
    ThreadPool::~ThreadPool() {
        shutdown();
    }
    // ThreadPool::ThreadPool(ThreadPool&& other) noexcept
    //     : workers_(std::move(other.workers_))
    //     , tasks_(std::move(other.tasks_))
    //     , high_priority_tasks_(std::move(other.high_priority_tasks_))
    //     , stop_(other.stop_.load())
    //     , paused_(other.paused_.load())
    //     , next_task_id_(other.next_task_id_.load())
    //     , completed_tasks_(other.completed_tasks_.load())
    //     , failed_tasks_(other.failed_tasks_.load())
    //     , active_thread_count_(other.active_thread_count_.load())
    //     , busy_threads_(other.busy_threads_.load())
    //     , threads_to_stop_(other.threads_to_stop_.load())
    //     , config_(std::move(other.config_))
    //     , min_threads_(other.min_threads_)
    //     , max_threads_(other.max_threads_)
    //     , error_handler_(std::move(other.error_handler_)) {}

    // ThreadPool& ThreadPool::operator=(ThreadPool&& other) noexcept {
    //     if (this != &other) {
    //         shutdown();
    //         workers_ = std::move(other.workers_);
    //         tasks_ = std::move(other.tasks_);
    //         high_priority_tasks_ = std::move(other.high_priority_tasks_);
    //         stop_.store(other.stop_.load());
    //         paused_.store(other.paused_.load());
    //         next_task_id_.store(other.next_task_id_.load());
    //         completed_tasks_.store(other.completed_tasks_.load());
    //         failed_tasks_.store(other.failed_tasks_.load());
    //         active_thread_count_.store(other.active_thread_count_.load());
    //         busy_threads_.store(other.busy_threads_.load());
    //         threads_to_stop_.store(other.threads_to_stop_.load());
    //         config_ = std::move(other.config_);
    //         min_threads_ = other.min_threads_;
    //         max_threads_ = other.max_threads_;
    //         error_handler_ = std::move(other.error_handler_);
    //     }
    //     return *this;
    // }
    void ThreadPool::init(size_t numThreads) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back();
            ThreadContext& ctx = workers_.back();
            ctx.thread = std::thread(&ThreadPool::worker, this, &ctx);
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
    while (true) {
        Task task;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_.load() || threads_to_stop_.load() > 0 || (!paused_.load() && (!high_priority_tasks_.empty() || !tasks_.empty())) ;});
            if (stop_.load()) break;
            if (threads_to_stop_.load() > 0) {
                threads_to_stop_.fetch_sub(1);
                break;
            }
            if (paused_.load()) continue;
            if (!high_priority_tasks_.empty()) {
                task = std::move(high_priority_tasks_.front());
                high_priority_tasks_.pop_front();
            }
            else if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            else {
                continue;
            }
        }
        busy_threads_.fetch_add(1);
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
        busy_threads_.fetch_sub(1);
        ctx->task_processed.fetch_add(1);
        wait_cv_.notify_all();
    }
    ctx->active.store(false);
    active_thread_count_.fetch_sub(1);
}
    void ThreadPool::enqueue(std::function<void()> task) {
        if (stop_.load()) {
            throw std::runtime_error("Can't enqueue");
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
        if (new_size < min_threads_ || new_size > max_threads_)
            return false;
        size_t current = workers_.size();
        if (new_size > current) {
            for (size_t i = current; i < new_size; ++i) {
                workers_.emplace_back();
                ThreadContext& ctx = workers_.back();
                ctx.thread = std::thread(&ThreadPool::worker, this, &ctx);
            }
        }
        else if (new_size < current) {
            size_t to_remove = current - new_size;
            threads_to_stop_.store(to_remove);
            cv_.notify_all();
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (active_thread_count_.load() > new_size && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            auto it = workers_.begin();
            while (it != workers_.end()) {
                if (!it->active.load()) {
                    if (it->thread.joinable()) {
                        it->thread.join();
                    }
                    it = workers_.erase(it);
                }
                else {
                    ++it;
                }
            }
            threads_to_stop_.store(0);
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
        wait_cv_.wait(lock, [this] { return tasks_.empty() && high_priority_tasks_.empty() && completed_tasks_.load() + failed_tasks_.load() >= next_task_id_.load() ;});
    }
    bool ThreadPool::waitAllFor(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return wait_cv_.wait_for(lock, timeout, [this] {
            return tasks_.empty() && high_priority_tasks_.empty() && completed_tasks_.load() + failed_tasks_.load() >= next_task_id_.load() ;});
    }
    size_t ThreadPool::activeThreads() const noexcept {
        return busy_threads_.load();
    }
    ThreadPoolStats ThreadPool::getStats() const {
        ThreadPoolStats stats;
        stats.active_threads = busy_threads_.load();
        stats.idle_threads = workers_.size() - stats.active_threads;
        stats.pending_tasks = tasks_.size() + high_priority_tasks_.size();
        stats.total_tasks_completed = completed_tasks_.load();
        stats.total_tasks_failed = failed_tasks_.load();
        return stats;
    }
    void ThreadPool::setErrorHandler(std::function<void(std::exception_ptr)> handler) {
        error_handler_ = std::move(handler);
    }

}