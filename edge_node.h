#ifndef EDGE_NODE_H
#define EDGE_NODE_H

#include "../task/task.h"
#include <string>
#include <queue>
#include <memory>
#include <mutex>
#include <chrono>
#include <vector>

/**
 * Represents a simulated edge computing node
 * Stores resource capacities and current usage
 */
class EdgeNode {
public:
    struct ResourceCapacity {
        double cpu_percent;          // Total CPU capacity (100%)
        double ram_mb;               // Total RAM capacity
        double bandwidth_mbps;       // Total bandwidth capacity
        int64_t latency_ms;          // Network latency to this edge
    };

    struct ResourceUsage {
        double cpu_percent;
        double ram_mb;
        double bandwidth_mbps;
    };

    EdgeNode(const std::string& edge_id, const ResourceCapacity& capacity);

    // Getters
    std::string getEdgeId() const { return edge_id_; }
    ResourceCapacity getCapacity() const { return capacity_; }
    ResourceUsage getCurrentUsage() const;
    bool isAvailable() const { return is_available_; }
    
    // Resource queries
    double getAvailableCpuPercent() const;
    double getAvailableRamMb() const;
    double getAvailableBandwidthMbps() const;
    int64_t getLatencyMs() const { return capacity_.latency_ms; }
    int64_t getQueueLength() const;
    std::vector<std::shared_ptr<Task>> getQueuedTasks() const;

    // Task management
    void enqueueTask(std::shared_ptr<Task> task);
    std::shared_ptr<Task> dequeueTask();
    bool hasTasksInQueue() const;
    const std::queue<std::shared_ptr<Task>>& getTaskQueue() const { return task_queue_; }

    // Resource allocation
    bool canAllocate(double cpu, double ram, double bandwidth) const;
    void allocateResources(double cpu, double ram, double bandwidth);
    void deallocateResources(double cpu, double ram, double bandwidth);

    // Status management
    void setAvailable(bool available) { is_available_ = available; }
    void setLatencyMs(int64_t latency) { capacity_.latency_ms = latency; }

    // Simulation helpers - simulate resource fluctuations
    void simulateResourceFluctuation();

    // Statistics
    uint64_t getTotalTasksProcessed() const { return total_tasks_processed_; }
    uint64_t getTotalTasksFailed() const { return total_tasks_failed_; }

private:
    std::string edge_id_;
    ResourceCapacity capacity_;
    ResourceUsage current_usage_;
    std::queue<std::shared_ptr<Task>> task_queue_;
    mutable std::mutex resource_mutex_;
    bool is_available_;
    uint64_t total_tasks_processed_;
    uint64_t total_tasks_failed_;

    void updateResourceUsage();
};

#endif // EDGE_NODE_H
