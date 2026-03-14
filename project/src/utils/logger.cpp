#include <format>
#include "utils/logger.h"

#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

namespace {
// helper to get the time right without crashing on different platforms
std::tm make_local_time(std::time_t time_value) {
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &time_value);
#else
    localtime_r(&time_value, &result);
#endif
    return result;
}
}

namespace redis::utils {

// the one and only way to get the logger instance
Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

// default setup for the logger
Logger::Logger() = default;

// cleanup when the server dies
Logger::~Logger() {
    flush();

    std::lock_guard<std::mutex> file_lock(file_mutex_);
    if (file_ && file_->is_open()) {
        file_->close();
    }
}

// update the settings on the fly
void Logger::configure(const LoggerConfig& cfg) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = cfg;
        level_.store(cfg.min_level, std::memory_order_relaxed);
    }

    std::lock_guard<std::mutex> file_lock(file_mutex_);
    if (file_ && file_->is_open()) {
        file_->flush();
        file_->close();
    }

    file_.reset();
    current_file_size_ = 0;

    if (cfg.log_to_file) {
        open_log_file();
    }
}

// change the filter level for messages
void Logger::set_level(Level level) {
    level_.store(level, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(mutex_);
    config_.min_level = level;
}

// capture the log message if it is important enough
void Logger::log(Level level, const std::string& message, std::source_location loc) {
    if (static_cast<int>(level) < static_cast<int>(level_.load(std::memory_order_relaxed))) {
        return; // not important enough for us right now (get ignored lmao)
    }

    LogEntry entry{level, message, std::chrono::system_clock::now(), loc};
    log_impl(entry);
}

// deadahh, actual heavy lifting for writing logs
void Logger::log_impl(const LogEntry& entry) {
    LoggerConfig cfg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cfg = config_;
    }

    const std::string formatted = format_entry(entry);

    // send to terminal if configured
    if (cfg.log_to_console) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (entry.level == Level::ERROR) {
            std::cerr << formatted; // serious stuff goes to stderr
            std::cerr.flush();
        } else {
            std::clog << formatted; // normal stuff goes to clog
            std::clog.flush();
        }
    }

    if (!cfg.log_to_file) {
        return;
    }

    std::lock_guard<std::mutex> file_lock(file_mutex_);

    if (!file_ || !file_->is_open()) {
        open_log_file();
    }

    if (!file_ || !file_->is_open()) {
        return; // file is being stubborn, give up, or not
    }

    // handle rotation if the file is getting too fat
    if (cfg.max_file_size > 0 && current_file_size_ + formatted.size() > cfg.max_file_size) {
        rotate_files();
        open_log_file();
    }

    if (!file_ || !file_->is_open()) {
        return;
    }

    (*file_) << formatted;
    if (file_->good()) {
        current_file_size_ += formatted.size();
        file_->flush(); // write it now so we don't lose it if we crash
    }
}

// shift old logs around to make room for new ones
void Logger::rotate_files() {
    LoggerConfig cfg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cfg = config_;
    }

    if (file_ && file_->is_open()) {
        file_->flush();
        file_->close();
    }

    file_.reset();
    current_file_size_ = 0;

    namespace fs = std::filesystem;
    std::error_code ec;

    if (cfg.filename.empty()) {
        return;
    }

    if (cfg.max_files == 0) {
        fs::remove(cfg.filename, ec);
        return;
    }

    // delete the oldest file
    fs::remove(cfg.filename + "." + std::to_string(cfg.max_files), ec);

    // shuffle the rest
    for (std::size_t i = cfg.max_files; i > 1; --i) {
        const std::string from = cfg.filename + "." + std::to_string(i - 1);
        const std::string to = cfg.filename + "." + std::to_string(i);

        if (fs::exists(from, ec)) {
            fs::remove(to, ec);
            fs::rename(from, to, ec);
        }
    }

    // move current to .1
    if (fs::exists(cfg.filename, ec)) {
        fs::remove(cfg.filename + ".1", ec);
        fs::rename(cfg.filename, cfg.filename + ".1", ec);
    }
}

// map the enum to a string that english speaking humans can read
std::string Logger::level_to_string(Level level) const {
    switch (level) {
        case Level::DEBUG: return "DEBUG";
        case Level::INFO:  return "INFO";
        case Level::WARN:  return "WARN";
        case Level::ERROR: return "ERROR";
        default:           return "UNKNOWN";
    }
}

// make the log line look pretty (or at least readable (we are not ui experts in this bih))
std::string Logger::format_entry(const LogEntry& entry) const {
    LoggerConfig cfg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cfg = config_;
    }

    std::ostringstream stream;

    // add the date and time if wanted
    if (cfg.include_timestamp) {
        const std::time_t time_value = std::chrono::system_clock::to_time_t(entry.timestamp);
        const std::tm local_time = make_local_time(time_value);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            entry.timestamp.time_since_epoch()) % 1000;

        stream << '['
               << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S")
               << '.' << std::setw(3) << std::setfill('0') << ms.count()
               << "] ";
    }

    stream << '[' << level_to_string(entry.level) << "] " << entry.message;

    // show exactly where the log came from so we can fix it
    if (cfg.include_location) {
        stream << " (" << entry.location.file_name() << ':' << entry.location.line()
               << ' ' << entry.location.function_name() << ')';
    }

    stream << '\n';
    return stream.str();
}

// try to open the log file without breaking anything
void Logger::open_log_file() {
    LoggerConfig cfg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cfg = config_;
    }

    if (!cfg.log_to_file || cfg.filename.empty()) {
        file_.reset();
        current_file_size_ = 0;
        return;
    }

    file_ = std::make_unique<std::ofstream>(cfg.filename, std::ios::out | std::ios::app);
    if (!file_->is_open()) {
        file_.reset();
        current_file_size_ = 0;
        return;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    current_file_size_ = fs::exists(cfg.filename, ec)
        ? static_cast<std::size_t>(fs::file_size(cfg.filename, ec))
        : 0;

    if (ec) {
        current_file_size_ = 0;
    }
}

// push everything out to the os
void Logger::flush() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::clog.flush();
        std::cerr.flush();
    }

    std::lock_guard<std::mutex> file_lock(file_mutex_);
    if (file_ && file_->is_open()) {
        file_->flush();
    }
}

}