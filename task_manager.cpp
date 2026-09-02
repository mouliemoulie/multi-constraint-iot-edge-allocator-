#include "task/task_manager.h"
#include "task/task_profile.h"
#include "common/logger.h"

#include <sstream>
#include <iomanip>

TaskManager::TaskManager()
    : total_tasks_created_(0U),
      total_tasks_processed_(0U),
      task_counter_(0U) {
}

std::string TaskManager::generateTaskId() {
    ++task_counter_;
    std::ostringstream oss;
    oss << "T" << std::setfill('0') << std::setw(6) << task_counter_;
    return oss.str();
}

std::shared_ptr<Task> TaskManager::createTaskFromSensorData(
    const std::string& device_id,
    const std::string& sensor_type,
    const json& sensor_data) {

    // Spec §5: "The Task Manager receives [sensor data] and identifies
    // the appropriate task profile" -> "The Task Manager creates: Task".
    const Task::TaskRequirements requirements =
        TaskProfile::getInstance().getRequirements(sensor_type);

    const std::string task_id = generateTaskId();

    auto task = std::make_shared<Task>(
        task_id, device_id, sensor_type, requirements, sensor_data);

    ++total_tasks_created_;

    Logger::getInstance().debug("TaskManager",
        "Created task " + task_id + " for device " + device_id +
        " (" + sensor_type + ")");

    if (task_creation_callback_) {
        task_creation_callback_(task);
    }

    return task;
}

void TaskManager::enqueueTask(std::shared_ptr<Task> task) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    task_queue_.push(task);
}

std::shared_ptr<Task> TaskManager::dequeueTask() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (task_queue_.empty()) {
        return nullptr;
    }

    std::shared_ptr<Task> task = task_queue_.front();
    task_queue_.pop();
    ++total_tasks_processed_;
    return task;
}

bool TaskManager::hasTasksInQueue() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return !task_queue_.empty();
}

size_t TaskManager::getQueueSize() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return task_queue_.size();
}

void TaskManager::registerTaskCreationCallback(TaskCallback callback) {
    task_creation_callback_ = std::move(callback);
}

void TaskManager::unregisterTaskCreationCallback() {
    task_creation_callback_ = nullptr;
}
