#include "utils/timer.h"

namespace redis::utils {

// default constructor for the stopwatch
Stopwatch::Stopwatch() noexcept = default;

// start right away if the user is in a hurry
Stopwatch::Stopwatch(bool start_immediately) noexcept {
    if (start_immediately) {
        start();
    }
}

// kick off the timer
void Stopwatch::start() noexcept {
    if (running_) {
        return; // already going, no need to do anything
    }

    start_time_ = clock::now();
    running_ = true;
}

// save how much time passed
void Stopwatch::stop() noexcept {
    if (!running_) {
        return; // nothing to stop
    }

    accumulated_ += clock::now() - start_time_;
    running_ = false;
}

// wipe the memory and start over from zero
void Stopwatch::reset() noexcept {
    accumulated_ = duration::zero();
    start_time_ = time_point{};
    running_ = false;
}

// reset and start immediately
void Stopwatch::restart() noexcept {
    accumulated_ = duration::zero();
    start_time_ = clock::now();
    running_ = true;
}

// check if the clock is still ticking
bool Stopwatch::running() const noexcept {
    return running_;
}

// calculate total time, including the current run if it is active
Stopwatch::duration Stopwatch::elapsed() const noexcept {
    if (!running_) {
        return accumulated_;
    }

    return accumulated_ + (clock::now() - start_time_);
}

// helper for milliseconds
std::chrono::milliseconds Stopwatch::elapsed_ms() const noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed());
}

// helper for microseconds (extra precision)
std::chrono::microseconds Stopwatch::elapsed_us() const noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(elapsed());
}

// tbh i would have added even more precise for goofs and laughs

// default constructor for the deadline
DeadlineTimer::DeadlineTimer() noexcept = default;

// set a deadline immediately upon creation
DeadlineTimer::DeadlineTimer(duration timeout) noexcept {
    arm(timeout);
}

// set the timer for a specific duration from now
void DeadlineTimer::arm(duration timeout) noexcept {
    if (timeout < duration::zero()) {
        timeout = duration::zero(); // negative time isnt a thing yet
    }

    timeout_ = timeout;
    deadline_ = clock::now() + timeout;
    active_ = true;
}

// just an alias for arming to make the code read better
void DeadlineTimer::rearm(duration timeout) noexcept {
    arm(timeout);
}

// cancel the deadline and clear everything
void DeadlineTimer::disarm() noexcept {
    deadline_ = time_point{};
    timeout_ = duration::zero();
    active_ = false;
}

// is the timer currently armed
bool DeadlineTimer::active() const noexcept {
    return active_;
}

// check if the time has run out
bool DeadlineTimer::expired() const noexcept {
    return active_ && clock::now() >= deadline_;
}

// see how much time we have left
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

// find out what the original timeout was
DeadlineTimer::duration DeadlineTimer::timeout() const noexcept {
    return timeout_;
}

// get the exact time point of the deadline
DeadlineTimer::time_point DeadlineTimer::deadline() const noexcept {
    return deadline_;
}

// default constructor for the ticker
PeriodicTimer::PeriodicTimer() noexcept = default;

// start looping at a specific interval
PeriodicTimer::PeriodicTimer(duration interval) noexcept {
    start(interval);
}

// setup the loop and set the first target
void PeriodicTimer::start(duration interval) noexcept {
    if (interval <= duration::zero()) {
        stop(); // cant loop at zero speed
        return;
    }

    interval_ = interval;
    next_tick_ = clock::now() + interval;
    active_ = true;
}

// shut down the periodic loop
void PeriodicTimer::stop() noexcept {
    next_tick_ = time_point{};
    interval_ = duration::zero();
    active_ = false;
}

// move the target forward after a tick is handled
void PeriodicTimer::consume() noexcept {
    if (!active_ || interval_ <= duration::zero()) {
        return;
    }

    const time_point now = clock::now();
    if (now < next_tick_) {
        return; // too early
    }

    // skip missed intervals if we are lagging behind
    do {
        next_tick_ += interval_;
    } while (next_tick_ <= now);
}

// is it still active and looping
bool PeriodicTimer::active() const noexcept {
    return active_;
}

// check if it is time to do something
bool PeriodicTimer::ready() const noexcept {
    return active_ && clock::now() >= next_tick_;
}

// what is the current loop delay
PeriodicTimer::duration PeriodicTimer::interval() const noexcept {
    return interval_;
}

// when exactly is the next tick coming
PeriodicTimer::time_point PeriodicTimer::next_tick() const noexcept {
    return next_tick_;
}

}