#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include "task.h"
#include <queue>
#include <memory>
#include <mutex>
#include <functional>

/**
 * Task Manager converts sensor data into computational tasks
 * Manages task creation, queuing, and distribution
 */
class TaskManager {
public:
    using TaskCallback = std::function<void(std::shared_ptr<Task>)>;

    TaskManager();
    
    // Create task from sensor data
    std::shared_ptr<Task> createTaskFromSensorData(
        const std::string& device_id,
        const std::string& sensor_type,
        const json& sensor_data
    );

    // Task queue management
    void enqueueTask(std::shared_ptr<Task> task);
    std::shared_ptr<Task> dequeueTask();
    bool hasTasksInQueue() const;
    size_t getQueueSize() const;

    // Register callback for new tasks (e.g., for resource allocator)
    void registerTaskCreationCallback(TaskCallback callback);
    void unregisterTaskCreationCallback();

    // Statistics
    uint64_t getTotalTasksCreated() const { return total_tasks_created_; }
    uint64_t getTotalTasksProcessed() const { return total_tasks_processed_; }

    // Generate unique task ID
    std::string generateTaskId();

private:
    std::queue<std::shared_ptr<Task>> task_queue_;
    mutable std::mutex queue_mutex_;
    TaskCallback task_creation_callback_;
    uint64_t total_tasks_created_;
    uint64_t total_tasks_processed_;
    uint64_t task_counter_;
};

#endif // TASK_MANAGER_H
