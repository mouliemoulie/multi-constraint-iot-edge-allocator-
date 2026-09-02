#include "executor/task_executor.h"
#include "database/database_manager.h"
#include "common/logger.h"
#include "common/constants.h"

#include <chrono>
#include <random>
#include <nlohmann/json.hpp>

TaskExecutor::TaskExecutor(std::shared_ptr<TaskScheduler> scheduler)
    : scheduler_(std::move(scheduler)),
      is_running_(false),
      total_tasks_executed_(0U),
      total_tasks_failed_(0U),
      total_execution_time_ms_(0.0) {
}

TaskExecutor::~TaskExecutor() {
    stop();
}

void TaskExecutor::registerEdgeNode(std::shared_ptr<EdgeNode> node) {
    if (node == nullptr) {
        return;
    }
    edge_nodes_[node->getEdgeId()] = node;
}

void TaskExecutor::unregisterEdgeNode(const std::string& edge_id) {
    edge_nodes_.erase(edge_id);
}

void TaskExecutor::start() {
    if (is_running_) {
        return;
    }
    is_running_ = true;

    for (const auto& entry : edge_nodes_) {
        const std::string& edge_id = entry.first;
        execution_threads_[edge_id] = std::thread(
            &TaskExecutor::executeTasksForEdge, this, entry.second);
    }

    Logger::getInstance().info("TaskExecutor",
        "Started with " + std::to_string(execution_threads_.size()) + " edge worker thread(s).");
}

void TaskExecutor::stop() {
    if (!is_running_) {
        return;
    }
    is_running_ = false;

    for (auto& entry : execution_threads_) {
        if (entry.second.joinable()) {
            entry.second.join();
        }
    }
    execution_threads_.clear();

    Logger::getInstance().info("TaskExecutor", "Stopped.");
}

void TaskExecutor::executeTasksForEdge(std::shared_ptr<EdgeNode> edge) {
    while (is_running_) {
        std::shared_ptr<Task> task = scheduler_->getNextTask(edge);

        if (task != nullptr) {
            simulateTaskExecution(edge, task);
        } else {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(constants::kSimulationTickIntervalMs));
        }
    }
}

int64_t TaskExecutor::estimateExecutionTime(std::shared_ptr<Task> task) {
    // A simple, deterministic-ish model: execution time scales with CPU
    // requirement (heavier tasks take proportionally longer), with a
    // small amount of jitter to model real-world variance. This keeps
    // the "algorithm, not AI/ML" framing from the spec (§1: "without
    // AI/ML") while still producing varied, comparable timing data
    // across allocation strategies (spec §17-18 metrics).
    const Task::TaskRequirements requirements = task->getRequirements();

    constexpr double kBaseMs = 5.0;
    constexpr double kCpuScalingFactor = 0.8;

    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_real_distribution<double> jitter(0.9, 1.3);

    const double estimated = (kBaseMs + (requirements.cpu_percent * kCpuScalingFactor)) * jitter(generator);
    return static_cast<int64_t>(estimated);
}

void TaskExecutor::simulateTaskExecution(std::shared_ptr<EdgeNode> edge, std::shared_ptr<Task> task) {
    task->setStatus(Task::TaskStatus::RUNNING);
    DatabaseManager::getInstance().updateTaskStatus(task->getTaskId(), Task::TaskStatus::RUNNING);
    {
        std::lock_guard<std::mutex> lock(active_tasks_mutex_);
        active_tasks_[task->getTaskId()] = task;
    }

    const int64_t estimated_ms = estimateExecutionTime(task);
    std::this_thread::sleep_for(std::chrono::milliseconds(estimated_ms));

    const bool met_deadline = !task->isDeadlineExceeded();
    task->setCompletionTime(std::chrono::system_clock::now());
    task->setStatus(met_deadline ? Task::TaskStatus::COMPLETED : Task::TaskStatus::FAILED);
    DatabaseManager::getInstance().updateTaskStatus(
        task->getTaskId(), met_deadline ? Task::TaskStatus::COMPLETED : Task::TaskStatus::FAILED);
    {
        std::lock_guard<std::mutex> lock(active_tasks_mutex_);
        active_tasks_.erase(task->getTaskId());
    }

    const Task::TaskRequirements requirements = task->getRequirements();
    edge->deallocateResources(requirements.cpu_percent, requirements.ram_mb, requirements.bandwidth_mbps);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (met_deadline) {
            ++total_tasks_executed_;
        } else {
            ++total_tasks_failed_;
        }
        total_execution_time_ms_ += static_cast<double>(estimated_ms);
    }

    DatabaseManager::getInstance().saveTaskExecution(task->getTaskId(), estimated_ms, met_deadline);

    Logger::getInstance().debug("TaskExecutor",
        "Task " + task->getTaskId() + " on " + edge->getEdgeId() +
        " completed in " + std::to_string(estimated_ms) + "ms" +
        (met_deadline ? "" : " (DEADLINE MISSED)"));
}

uint64_t TaskExecutor::getTotalTasksExecuted() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return total_tasks_executed_;
}

uint64_t TaskExecutor::getTotalTasksFailed() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return total_tasks_failed_;
}

double TaskExecutor::getAverageExecutionTimeMs() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    const uint64_t completed = total_tasks_executed_ + total_tasks_failed_;
    if (completed == 0U) {
        return 0.0;
    }
    return total_execution_time_ms_ / static_cast<double>(completed);
}

std::vector<nlohmann::json> TaskExecutor::getActiveTasks() const {
    std::lock_guard<std::mutex> lock(active_tasks_mutex_);
    std::vector<nlohmann::json> result;
    result.reserve(active_tasks_.size());
    for (const auto& entry : active_tasks_) {
        const auto& task = entry.second;
        nlohmann::json item = task->toJson();
        item["elapsed_ms"] = task->getElapsedTimeMs();
        item["remaining_ms"] = task->getRemainingTimeMs();
        result.push_back(item);
    }
    return result;
}

double TaskExecutor::getAverageLatency() const {
    // Average network latency across all registered edge nodes,
    // weighted equally (a simple aggregate metric for reporting).
    if (edge_nodes_.empty()) {
        return 0.0;
    }

    double total_latency = 0.0;
    for (const auto& entry : edge_nodes_) {
        total_latency += static_cast<double>(entry.second->getLatencyMs());
    }
    return total_latency / static_cast<double>(edge_nodes_.size());
}
