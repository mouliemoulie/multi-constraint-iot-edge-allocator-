#include "task/task.h"

Task::Task(
    const std::string& task_id,
    const std::string& device_id,
    const std::string& sensor_type,
    const TaskRequirements& requirements,
    const json& sensor_data)
    : task_id_(task_id),
      device_id_(device_id),
      sensor_type_(sensor_type),
      requirements_(requirements),
      sensor_data_(sensor_data),
      status_(TaskStatus::CREATED),
      assigned_edge_id_(""),
      creation_time_(std::chrono::system_clock::now()),
      completion_time_(std::chrono::system_clock::time_point{}) {
}

int64_t Task::getElapsedTimeMs() const {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now - creation_time_).count();
}

int64_t Task::getRemainingTimeMs() const {
    return requirements_.deadline_ms - getElapsedTimeMs();
}

bool Task::isDeadlineExceeded() const {
    return getElapsedTimeMs() > requirements_.deadline_ms;
}

json Task::toJson() const {
    json j;
    j["task_id"] = task_id_;
    j["device_id"] = device_id_;
    j["sensor_type"] = sensor_type_;
    j["cpu_percent"] = requirements_.cpu_percent;
    j["ram_mb"] = requirements_.ram_mb;
    j["bandwidth_mbps"] = requirements_.bandwidth_mbps;
    j["deadline_ms"] = requirements_.deadline_ms;
    j["priority"] = static_cast<int>(requirements_.priority);
    j["status"] = static_cast<int>(status_);
    j["assigned_edge_id"] = assigned_edge_id_;
    j["sensor_data"] = sensor_data_;
    return j;
}

Task Task::fromJson(const json& j) {
    TaskRequirements requirements;
    requirements.cpu_percent = j.at("cpu_percent").get<double>();
    requirements.ram_mb = j.at("ram_mb").get<double>();
    requirements.bandwidth_mbps = j.at("bandwidth_mbps").get<double>();
    requirements.deadline_ms = j.at("deadline_ms").get<int64_t>();
    requirements.priority = static_cast<TaskPriority>(j.at("priority").get<int>());

    Task task(
        j.at("task_id").get<std::string>(),
        j.at("device_id").get<std::string>(),
        j.at("sensor_type").get<std::string>(),
        requirements,
        j.value("sensor_data", json::object())
    );

    if (j.contains("assigned_edge_id")) {
        task.setAssignedEdgeId(j.at("assigned_edge_id").get<std::string>());
    }
    if (j.contains("status")) {
        task.setStatus(static_cast<TaskStatus>(j.at("status").get<int>()));
    }

    return task;
}
