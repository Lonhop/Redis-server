#ifndef REDIS_SERVER_TIMER_H
#define REDIS_SERVER_TIMER_H

#include <chrono>

namespace redis::utils {

class Stopwatch {
public:
    using clock = std::chrono::steady_clock;
    using duration = clock::duration;
    using time_point = clock::time_point;

    Stopwatch() noexcept;
    explicit Stopwatch(bool start_immediately) noexcept;

    void start() noexcept;
    void stop() noexcept;
    void reset() noexcept;
    void restart() noexcept;

    bool running() const noexcept;
    duration elapsed() const noexcept;
    std::chrono::milliseconds elapsed_ms() const noexcept;
    std::chrono::microseconds elapsed_us() const noexcept;

private:
    time_point start_time_{};
    duration accumulated_{duration::zero()};
    bool running_ = false;
};

class DeadlineTimer {
public:
    using clock = std::chrono::steady_clock;
    using duration = clock::duration;
    using time_point = clock::time_point;

    DeadlineTimer() noexcept;
    explicit DeadlineTimer(duration timeout) noexcept;

    void arm(duration timeout) noexcept;
    void rearm(duration timeout) noexcept;
    void disarm() noexcept;

    bool active() const noexcept;
    bool expired() const noexcept;
    duration remaining() const noexcept;
    duration timeout() const noexcept;
    time_point deadline() const noexcept;

private:
    time_point deadline_{};
    duration timeout_{duration::zero()};
    bool active_ = false;
};

class PeriodicTimer {
public:
    using clock = std::chrono::steady_clock;
    using duration = clock::duration;
    using time_point = clock::time_point;

    PeriodicTimer() noexcept;
    explicit PeriodicTimer(duration interval) noexcept;

    void start(duration interval) noexcept;
    void stop() noexcept;
    void consume() noexcept;

    bool active() const noexcept;
    bool ready() const noexcept;
    duration interval() const noexcept;
    time_point next_tick() const noexcept;

private:
    time_point next_tick_{};
    duration interval_{duration::zero()};
    bool active_ = false;
};

}

#endif