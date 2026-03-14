#ifndef REDIS_SERVER_RAII_HELPERS_H
#define REDIS_SERVER_RAII_HELPERS_H

#include <atomic>
#include <cstddef>
#include <functional>
#include <utility>

namespace redis::utils {

// basically a safety net that runs code when you leave a function
// useful for closing stuff you forgot to close
// after all we are big boys and we want our stuff secured
class ScopeExit {
public:
    // give it a function to run later
    explicit ScopeExit(std::function<void()> callback) noexcept;

    // no copying allowed (would be a mess)
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    // move it if you really have to
    ScopeExit(ScopeExit&& other) noexcept;
    ScopeExit& operator=(ScopeExit&& other) noexcept;

    // the callback actually runs
    ~ScopeExit();

    // cancel the whole thing
    void dismiss() noexcept;

    // check if it is still planning to run
    bool active() const noexcept;

private:
    std::function<void()> callback_;
    bool active_ = false;
};

// temporarily flips a switch and flips it back when done
class ScopedFlag {
public:
    // borrow a bool and hold it hostage with a new value
    explicit ScopedFlag(bool& flag, bool temporary_value = true) noexcept;
    
    // same thing but fancy (atomics)
    explicit ScopedFlag(std::atomic_bool& flag, bool temporary_value = true) noexcept;

    // i think a psa is needed to stop people from trying to copy the raii helpers
    ScopedFlag(const ScopedFlag&) = delete;
    ScopedFlag& operator=(const ScopedFlag&) = delete;
    ScopedFlag(ScopedFlag&&) = delete;
    ScopedFlag& operator=(ScopedFlag&&) = delete;

    // put the original value back where you found it
    ~ScopedFlag();

    // check what the value was before you messed with it
    bool previous() const noexcept;

private:
    bool* flag_ = nullptr;
    std::atomic_bool* atomic_flag_ = nullptr;
    bool previous_value_ = false;
};

// bump a number up and then down when done (who even reads this stuff)
class ScopedCounter {
public:
    // increment a counter now and decrement it at the end of the block
    explicit ScopedCounter(std::size_t& counter) noexcept;
    
    // same as above but for the atomic version
    explicit ScopedCounter(std::atomic_size_t& counter) noexcept;

    // copy constructors are still deleted (is grass green?)
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