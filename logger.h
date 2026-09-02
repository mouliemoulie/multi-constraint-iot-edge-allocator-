#ifndef COMMON_LOGGER_H
#define COMMON_LOGGER_H

#include <string>
#include <mutex>
#include <fstream>

/**
 * Single-point logging facility.
 *
 * MISRA deviation D4 (see docs/MISRA_DEVIATIONS.md): stream-based I/O is
 * restricted under MISRA. This is the ONLY place in the codebase that is
 * permitted to touch std::cout / std::cerr / std::ofstream directly, so
 * that a future MISRA-compliant sink swap touches exactly one file.
 *
 * Thread-safe: internal mutex serializes writes from multiple simulation
 * threads (IoT device generators, resource controller, task executor).
 */
class Logger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARN,
        ERROR
    };

    static Logger& getInstance();

    void setLogFile(const std::string& filepath);
    void setMinLevel(Level level) { min_level_ = level; }

    void log(Level level, const std::string& component, const std::string& message);

    void debug(const std::string& component, const std::string& message);
    void info(const std::string& component, const std::string& message);
    void warn(const std::string& component, const std::string& message);
    void error(const std::string& component, const std::string& message);

private:
    Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::mutex log_mutex_;
    std::ofstream log_file_;
    Level min_level_;
    bool file_enabled_;

    static std::string levelToString(Level level);
    static std::string currentTimestamp();
};

#endif  // COMMON_LOGGER_H
