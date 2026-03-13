#ifndef REDIS_SERVER_RAII_HELPERS_H
#define REDIS_SERVER_RAII_HELPERS_H

#include <atomic>
#include <cstddef>
#include <functional>
#include <utility>

namespace redis::utils {

class ScopeExit {
public:
    explicit ScopeExit(std::function<void()> callback) noexcept;

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    ScopeExit(ScopeExit&& other) noexcept;
    ScopeExit& operator=(ScopeExit&& other) noexcept;

    ~ScopeExit();

    void dismiss() noexcept;
    bool active() const noexcept;

private:
    std::function<void()> callback_;
    bool active_ = false;
};

class ScopedFlag {
public:
    explicit ScopedFlag(bool& flag, bool temporary_value = true) noexcept;
    explicit ScopedFlag(std::atomic_bool& flag, bool temporary_value = true) noexcept;

    ScopedFlag(const ScopedFlag&) = delete;
    ScopedFlag& operator=(const ScopedFlag&) = delete;
    ScopedFlag(ScopedFlag&&) = delete;
    ScopedFlag& operator=(ScopedFlag&&) = delete;

    ~ScopedFlag();

    bool previous() const noexcept;

private:
    bool* flag_ = nullptr;
    std::atomic_bool* atomic_flag_ = nullptr;
    bool previous_value_ = false;
};

class ScopedCounter {
public:
    explicit ScopedCounter(std::size_t& counter) noexcept;
    explicit ScopedCounter(std::atomic_size_t& counter) noexcept;

    ScopedCounter(const ScopedCounter&) = delete;
    ScopedCounter& operator=(const ScopedCounter&) = delete;
    ScopedCounter(ScopedCounter&&) = delete;
    ScopedCounter& operator=(ScopedCounter&&) = delete;

    ~ScopedCounter();

private:
    std::size_t* counter_ = nullptr;
    std::atomic_size_t* atomic_counter_ = nullptr;
};

}

#endif