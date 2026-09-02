#ifndef TASK_EXECUTOR_H
#define TASK_EXECUTOR_H

#include "../task/task.h"
#include "../edge/edge_node.h"
#include "../scheduler/task_scheduler.h"
#include <memory>
#include <thread>
#include <atomic>
#include <map>
#include <mutex>
#include <vector>
#include <nlohmann/json.hpp>

/**
 * Task Executor simulates task execution on edge nodes
 * Executes scheduled tasks and measures performance
 */
class TaskExecutor {
public:
    TaskExecutor(std::shared_ptr<TaskScheduler> scheduler);
    ~TaskExecutor();

    // Start/stop execution
    void start();
    void stop();
    bool isRunning() const { return is_running_; }

    // Register edge nodes for execution
    void registerEdgeNode(std::shared_ptr<EdgeNode> node);
    void unregisterEdgeNode(const std::string& edge_id);

    // Statistics
    uint64_t getTotalTasksExecuted() const;
    uint64_t getTotalTasksFailed() const;
    double getAverageExecutionTimeMs() const;
    double getAverageLatency() const;
    std::vector<nlohmann::json> getActiveTasks() const;

private:
    std::shared_ptr<TaskScheduler> scheduler_;
    std::map<std::string, std::shared_ptr<EdgeNode>> edge_nodes_;
    std::atomic<bool> is_running_;
    std::map<std::string, std::thread> execution_threads_;
    uint64_t total_tasks_executed_;
    uint64_t total_tasks_failed_;
    double total_execution_time_ms_;
    mutable std::mutex stats_mutex_;
    mutable std::mutex active_tasks_mutex_;
    std::map<std::string, std::shared_ptr<Task>> active_tasks_;

    // Execution loop for each edge node
    void executeTasksForEdge(std::shared_ptr<EdgeNode> edge);

    // Simulate task execution
    void simulateTaskExecution(
        std::shared_ptr<EdgeNode> edge,
        std::shared_ptr<Task> task
    );

    // Calculate execution time based on task requirements
    int64_t estimateExecutionTime(std::shared_ptr<Task> task);
};

#endif // TASK_EXECUTOR_H
