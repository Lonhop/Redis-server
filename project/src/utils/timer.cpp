#include "utils/timer.h"

namespace redis::utils {

Stopwatch::Stopwatch() noexcept = default;

Stopwatch::Stopwatch(bool start_immediately) noexcept {
    if (start_immediately) {
        start();
    }
}

void Stopwatch::start() noexcept {
    if (running_) {
        return;
    }

    start_time_ = clock::now();
    running_ = true;
}

void Stopwatch::stop() noexcept {
    if (!running_) {
        return;
    }

    accumulated_ += clock::now() - start_time_;
    running_ = false;
}

void Stopwatch::reset() noexcept {
    accumulated_ = duration::zero();
    start_time_ = time_point{};
    running_ = false;
}

void Stopwatch::restart() noexcept {
    accumulated_ = duration::zero();
    start_time_ = clock::now();
    running_ = true;
}

bool Stopwatch::running() const noexcept {
    return running_;
}

Stopwatch::duration Stopwatch::elapsed() const noexcept {
    if (!running_) {
        return accumulated_;
    }

    return accumulated_ + (clock::now() - start_time_);
}

std::chrono::milliseconds Stopwatch::elapsed_ms() const noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed());
}

std::chrono::microseconds Stopwatch::elapsed_us() const noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(elapsed());
}

DeadlineTimer::DeadlineTimer() noexcept = default;

DeadlineTimer::DeadlineTimer(duration timeout) noexcept {
    arm(timeout);
}

void DeadlineTimer::arm(duration timeout) noexcept {
    if (timeout < duration::zero()) {
        timeout = duration::zero();
    }

    timeout_ = timeout;
    deadline_ = clock::now() + timeout;
    active_ = true;
}

void DeadlineTimer::rearm(duration timeout) noexcept {
    arm(timeout);
}

void DeadlineTimer::disarm() noexcept {
    deadline_ = time_point{};
    timeout_ = duration::zero();
    active_ = false;
}

bool DeadlineTimer::active() const noexcept {
    return active_;
}

bool DeadlineTimer::expired() const noexcept {
    return active_ && clock::now() >= deadline_;
}

DeadlineTimer::duration DeadlineTimer::remaining() const noexcept {
    if (!active_) {
        return duration::zero();
    }

    const time_point now = clock::now();
    if (now >= deadline_) {
        return duration::zero();
    }

    return deadline_ - now;
}

DeadlineTimer::duration DeadlineTimer::timeout() const noexcept {
    return timeout_;
}

DeadlineTimer::time_point DeadlineTimer::deadline() const noexcept {
    return deadline_;
}

PeriodicTimer::PeriodicTimer() noexcept = default;

PeriodicTimer::PeriodicTimer(duration interval) noexcept {
    start(interval);
}

void PeriodicTimer::start(duration interval) noexcept {
    if (interval <= duration::zero()) {
        stop();
        return;
    }

    interval_ = interval;
    next_tick_ = clock::now() + interval;
    active_ = true;
}

void PeriodicTimer::stop() noexcept {
    next_tick_ = time_point{};
    interval_ = duration::zero();
    active_ = false;
}

void PeriodicTimer::consume() noexcept {
    if (!active_ || interval_ <= duration::zero()) {
        return;
    }

    const time_point now = clock::now();
    if (now < next_tick_) {
        return;
    }

    do {
        next_tick_ += interval_;
    } while (next_tick_ <= now);
}

bool PeriodicTimer::active() const noexcept {
    return active_;
}

bool PeriodicTimer::ready() const noexcept {
    return active_ && clock::now() >= next_tick_;
}

PeriodicTimer::duration PeriodicTimer::interval() const noexcept {
    return interval_;
}

PeriodicTimer::time_point PeriodicTimer::next_tick() const noexcept {
    return next_tick_;
}

}