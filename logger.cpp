#include "common/logger.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <array>

Logger::Logger()
    : min_level_(Level::INFO),
      file_enabled_(false) {
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::setLogFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_file_.open(filepath, std::ios::out | std::ios::app);
    file_enabled_ = log_file_.is_open();
}

std::string Logger::levelToString(Level level) {
    std::string result;
    switch (level) {
        case Level::DEBUG:
            result = "DEBUG";
            break;
        case Level::INFO:
            result = "INFO";
            break;
        case Level::WARN:
            result = "WARN";
            break;
        case Level::ERROR:
            result = "ERROR";
            break;
        default:
            result = "UNKNOWN";
            break;
    }
    return result;
}

std::string Logger::currentTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
    // localtime_r is the thread-safe variant; used deliberately over
    // std::localtime (which returns a pointer to shared static storage).
    localtime_r(&now, &tm_buf);

    std::array<char, 32> buffer{};
    const std::size_t written = std::strftime(
        buffer.data(), buffer.size(), "%Y-%m-%d %H:%M:%S", &tm_buf);
    if (written == 0U) {
        return std::string("0000-00-00 00:00:00");
    }
    return std::string(buffer.data());
}

void Logger::log(Level level, const std::string& component, const std::string& message) {
    if (level < min_level_) {
        return;
    }

    std::lock_guard<std::mutex> lock(log_mutex_);

    std::ostringstream line;
    line << "[" << currentTimestamp() << "] "
         << "[" << levelToString(level) << "] "
         << "[" << component << "] "
         << message;

    // Errors and warnings go to stderr; everything else to stdout.
    if (level == Level::ERROR || level == Level::WARN) {
        std::cerr << line.str() << std::endl;
    } else {
        std::cout << line.str() << std::endl;
    }

    if (file_enabled_ && log_file_.is_open()) {
        log_file_ << line.str() << std::endl;
    }
}

void Logger::debug(const std::string& component, const std::string& message) {
    log(Level::DEBUG, component, message);
}

void Logger::info(const std::string& component, const std::string& message) {
    log(Level::INFO, component, message);
}

void Logger::warn(const std::string& component, const std::string& message) {
    log(Level::WARN, component, message);
}

void Logger::error(const std::string& component, const std::string& message) {
    log(Level::ERROR, component, message);
}
