#include "utils/raii_helpers.h"

#include <utility>

namespace redis::utils {

// constructor for the safety net
ScopeExit::ScopeExit(std::function<void()> callback) noexcept
    : callback_(std::move(callback)), active_(static_cast<bool>(callback_)) {}

// move the responsibility to a new object
ScopeExit::ScopeExit(ScopeExit&& other) noexcept
    : callback_(std::move(other.callback_)), active_(other.active_) {
    other.active_ = false;
    other.callback_ = {};
}

// take over the safety net from someone else
ScopeExit& ScopeExit::operator=(ScopeExit&& other) noexcept {
    if (this == &other) {
        return *this; // already us
    }

    // if we had a job to do, do it now before we take the new one
    if (active_ && callback_) {
        try {
            callback_();
        } catch (...) {
            // we dont talk about exceptions in destructors
        }
    }

    callback_ = std::move(other.callback_);
    active_ = other.active_;

    other.active_ = false;
    other.callback_ = {};
    return *this;
}

// the moment we leave the room and the cleanup finally happens
ScopeExit::~ScopeExit() {
    if (!active_ || !callback_) {
        return;
    }

    try {
        callback_();
    } catch (...) {
        // seriously, keep your exceptions to yourself
    }
}

// stop the cleanup from happening
void ScopeExit::dismiss() noexcept {
    active_ = false;
}

// check if the net is still there
bool ScopeExit::active() const noexcept {
    return active_;
}

// grab a bool and flip it for a while
ScopedFlag::ScopedFlag(bool& flag, bool temporary_value) noexcept
    : flag_(&flag), previous_value_(flag) {
    *flag_ = temporary_value;
}

// same thing but for atomic flags so threads dont fight
ScopedFlag::ScopedFlag(std::atomic_bool& flag, bool temporary_value) noexcept
    : atomic_flag_(&flag), previous_value_(flag.exchange(temporary_value)) {}

// put the value back where we found it
ScopedFlag::~ScopedFlag() {
    if (flag_) {
        *flag_ = previous_value_;
    }

    if (atomic_flag_) {
        atomic_flag_->store(previous_value_);
    }
}

// see what the flag was before we messed with it
bool ScopedFlag::previous() const noexcept {
    return previous_value_;
}

// increment a counter just for being here
ScopedCounter::ScopedCounter(std::size_t& counter) noexcept
    : counter_(&counter) {
    ++(*counter_);
}

// atomic version for the thread-conscious developer (afonin egor)
ScopedCounter::ScopedCounter(std::atomic_size_t& counter) noexcept
    : atomic_counter_(&counter) {
    atomic_counter_->fetch_add(1, std::memory_order_relaxed);
}

// decrement the counter on the way out
ScopedCounter::~ScopedCounter() {
    if (counter_) {
        if (*counter_ > 0) {
            --(*counter_);
        }
    }

    if (atomic_counter_) {
        // safely decrement without going below zero
        std::size_t current = atomic_counter_->load(std::memory_order_relaxed);
        while (current > 0 && !atomic_counter_->compare_exchange_weak(
            current,
            current - 1,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
        }
    }
}

}