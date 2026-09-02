#include "task/task_profile.h"
#include "common/logger.h"

#include <sqlite3.h>

TaskProfile& TaskProfile::getInstance() {
    static TaskProfile instance;
    return instance;
}

void TaskProfile::initializeDefaultProfiles() {
    // Values taken directly from the project specification §6
    // "Where are these task requirements stored?"
    Task::TaskRequirements ultrasonic;
    ultrasonic.cpu_percent = 10.0;
    ultrasonic.ram_mb = 50.0;
    ultrasonic.bandwidth_mbps = 1.0;
    ultrasonic.deadline_ms = 100;
    ultrasonic.priority = Task::TaskPriority::HIGH;
    profiles_["ultrasonic"] = ultrasonic;

    Task::TaskRequirements temperature;
    temperature.cpu_percent = 5.0;
    temperature.ram_mb = 20.0;
    temperature.bandwidth_mbps = 0.5;
    temperature.deadline_ms = 500;
    temperature.priority = Task::TaskPriority::MEDIUM;
    profiles_["temperature"] = temperature;

    Task::TaskRequirements camera;
    camera.cpu_percent = 60.0;
    camera.ram_mb = 1024.0;  // 1 GB
    camera.bandwidth_mbps = 20.0;
    camera.deadline_ms = 50;
    camera.priority = Task::TaskPriority::CRITICAL;
    profiles_["camera"] = camera;

    // Reasonable extensions for the two additional sensor types this
    // implementation adds beyond the spec's three worked examples.
    Task::TaskRequirements humidity;
    humidity.cpu_percent = 5.0;
    humidity.ram_mb = 20.0;
    humidity.bandwidth_mbps = 0.5;
    humidity.deadline_ms = 500;
    humidity.priority = Task::TaskPriority::LOW;
    profiles_["humidity"] = humidity;

    Task::TaskRequirements pressure;
    pressure.cpu_percent = 5.0;
    pressure.ram_mb = 20.0;
    pressure.bandwidth_mbps = 0.5;
    pressure.deadline_ms = 500;
    pressure.priority = Task::TaskPriority::LOW;
    profiles_["pressure"] = pressure;
}

void TaskProfile::loadProfiles(const std::string& db_path) {
    // Spec §6: "They can be stored in SQLite or a configuration file
    // rather than being hard-coded into the source code." We attempt to
    // load from SQLite first; on any failure we fall back to sane
    // defaults so the simulator can still run standalone.
    initializeDefaultProfiles();

    sqlite3* db = nullptr;
    const int open_result = sqlite3_open_v2(
        db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);

    if (open_result != SQLITE_OK) {
        Logger::getInstance().warn("TaskProfile",
            "Could not open profile DB at " + db_path + "; using built-in defaults.");
        if (db != nullptr) {
            sqlite3_close(db);
        }
        return;
    }

    static const char* const kQuery =
        "SELECT sensor_type, cpu_percent, ram_mb, bandwidth_mbps, "
        "deadline_ms, priority FROM task_profiles;";

    sqlite3_stmt* stmt = nullptr;
    const int prepare_result = sqlite3_prepare_v2(db, kQuery, -1, &stmt, nullptr);

    if (prepare_result != SQLITE_OK) {
        Logger::getInstance().warn("TaskProfile",
            "task_profiles table not found; using built-in defaults.");
        sqlite3_close(db);
        return;
    }

    std::size_t loaded_count = 0U;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* type_text = sqlite3_column_text(stmt, 0);
        if (type_text == nullptr) {
            continue;
        }

        const std::string sensor_type(reinterpret_cast<const char*>(type_text));

        Task::TaskRequirements requirements;
        requirements.cpu_percent = sqlite3_column_double(stmt, 1);
        requirements.ram_mb = sqlite3_column_double(stmt, 2);
        requirements.bandwidth_mbps = sqlite3_column_double(stmt, 3);
        requirements.deadline_ms = sqlite3_column_int64(stmt, 4);
        requirements.priority = static_cast<Task::TaskPriority>(sqlite3_column_int(stmt, 5));

        profiles_[sensor_type] = requirements;
        ++loaded_count;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    Logger::getInstance().info("TaskProfile",
        "Loaded " + std::to_string(loaded_count) + " profile(s) from database.");
}

void TaskProfile::addProfile(const std::string& sensor_type, const Task::TaskRequirements& requirements) {
    profiles_[sensor_type] = requirements;
}

Task::TaskRequirements TaskProfile::getRequirements(const std::string& sensor_type) const {
    const auto it = profiles_.find(sensor_type);
    if (it != profiles_.end()) {
        return it->second;
    }

    // Unknown sensor type: return a conservative, low-priority default
    // rather than throwing, so a single unrecognized device type cannot
    // halt the whole simulation.
    Task::TaskRequirements fallback;
    fallback.cpu_percent = 5.0;
    fallback.ram_mb = 20.0;
    fallback.bandwidth_mbps = 0.5;
    fallback.deadline_ms = 1000;
    fallback.priority = Task::TaskPriority::LOW;
    return fallback;
}

bool TaskProfile::hasProfile(const std::string& sensor_type) const {
    return profiles_.find(sensor_type) != profiles_.end();
}
