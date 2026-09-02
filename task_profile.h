#ifndef TASK_PROFILE_H
#define TASK_PROFILE_H

#include "task.h"
#include <map>
#include <memory>

/**
 * Task profiles define the resource requirements for different sensor types
 * Profiles are stored in configuration/database, not hard-coded
 */
class TaskProfile {
public:
    static TaskProfile& getInstance();

    // Load profiles from database or config file
    void loadProfiles(const std::string& db_path);
    void addProfile(const std::string& sensor_type, const Task::TaskRequirements& requirements);

    // Get requirements for a sensor type
    Task::TaskRequirements getRequirements(const std::string& sensor_type) const;
    bool hasProfile(const std::string& sensor_type) const;

    // Get all profiles (for debugging/visualization)
    const std::map<std::string, Task::TaskRequirements>& getAllProfiles() const {
        return profiles_;
    }

private:
    TaskProfile() = default;
    TaskProfile(const TaskProfile&) = delete;
    TaskProfile& operator=(const TaskProfile&) = delete;

    std::map<std::string, Task::TaskRequirements> profiles_;

    // Default profiles if none are loaded
    void initializeDefaultProfiles();
};

#endif // TASK_PROFILE_H
