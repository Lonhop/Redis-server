#ifndef REDIS_SERVER_TIMER_H
#define REDIS_SERVER_TIMER_H

#include <chrono>

namespace redis::utils {

// basically a caveman timer
class Stopwatch {
public:
    using clock = std::chrono::steady_clock;
    using duration = clock::duration;
    using time_point = clock::time_point;

    Stopwatch() noexcept;
    explicit Stopwatch(bool start_immediately) noexcept;

    void start() noexcept;     // start the clock
    void stop() noexcept;      // stop the clock
    void reset() noexcept;     // back to zero
    void restart() noexcept;   // back to zero and go again

    bool running() const noexcept; // is it still ticking
    duration elapsed() const noexcept; // how long has it been
    std::chrono::milliseconds elapsed_ms() const noexcept; // time in ms
    std::chrono::microseconds elapsed_us() const noexcept; // time in us (fancy)

private:
    time_point start_time_{};
    duration accumulated_{duration::zero()};
    bool running_ = false;
};

// useful for when you need to give up after a while
class DeadlineTimer {
public:
    using clock = std::chrono::steady_clock;
    using duration = clock::duration;
    using time_point = clock::time_point;

    DeadlineTimer() noexcept;
    explicit DeadlineTimer(duration timeout) noexcept;

    void arm(duration timeout) noexcept;   // set the bomb
    void rearm(duration timeout) noexcept; // reset the fuse
    void disarm() noexcept;                // nevermind

    bool active() const noexcept;      // is it counting down
    bool expired() const noexcept;     // did we run out of time
    duration remaining() const noexcept; // how much time is left
    duration timeout() const noexcept;   // how long was the fuse
    time_point deadline() const noexcept; // when exactly it ends

private:
    time_point deadline_{};
    duration timeout_{duration::zero()};
    bool active_ = false;
};

// whoever reads this, hope you have a great day

// for things that need to happen over and over
// like life
class PeriodicTimer {
public:
    using clock = std::chrono::steady_clock;
    using duration = clock::duration;
    using time_point = clock::time_point;

    PeriodicTimer() noexcept;
    explicit PeriodicTimer(duration interval) noexcept;

    void start(duration interval) noexcept; // start the loop
    void stop() noexcept;                   // stop the loop
    void consume() noexcept;                // mark the current tick as done

    bool active() const noexcept;   // is it currently looping
    bool ready() const noexcept;    // is it time yet
    duration interval() const noexcept; // how long between ticks
    time_point next_tick() const noexcept; // when is the next one

private:
    time_point next_tick_{};
    duration interval_{duration::zero()};
    bool active_ = false;
};

}

#endif