#ifndef TASK_SCHEDULER_H
#define TASK_SCHEDULER_H

#include "../task/task.h"
#include "../edge/edge_node.h"
#include <queue>
#include <memory>
#include <functional>
#include <mutex>

/**
 * Task Scheduler determines execution order of tasks on each edge node
 * Different scheduling strategies can be implemented
 */
class TaskScheduler {
public:
    enum class SchedulingStrategy {
        FIFO,                    // First In First Out
        PRIORITY_BASED,          // By task priority
        EARLIEST_DEADLINE_FIRST, // EDF
        ROUND_ROBIN_TIME_SLICE   // Time slice round robin
    };

    TaskScheduler(SchedulingStrategy strategy = SchedulingStrategy::PRIORITY_BASED);

    // Schedule a task on an edge node
    void scheduleTask(std::shared_ptr<EdgeNode> edge, std::shared_ptr<Task> task);

    // Get next task to execute from edge node
    std::shared_ptr<Task> getNextTask(std::shared_ptr<EdgeNode> edge);

    // Strategy management
    void setStrategy(SchedulingStrategy strategy);
    SchedulingStrategy getStrategy() const;

    // Statistics
    uint64_t getTotalTasksScheduled() const { return total_tasks_scheduled_; }

private:
    SchedulingStrategy strategy_;
    uint64_t total_tasks_scheduled_;
    mutable std::mutex strategy_mutex_;

    // Comparators for different strategies
    struct FifoComparator {
        bool operator()(const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) const;
    };

    struct PriorityComparator {
        bool operator()(const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) const;
    };

    struct EdfComparator {
        bool operator()(const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) const;
    };
};

#endif // TASK_SCHEDULER_H
