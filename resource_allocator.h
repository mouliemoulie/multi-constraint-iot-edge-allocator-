#ifndef RESOURCE_ALLOCATOR_H
#define RESOURCE_ALLOCATOR_H

#include "../task/task.h"
#include "../edge/edge_node.h"
#include "../edge/resource_monitor.h"
#include <memory>
#include <map>
#include <vector>
#include <chrono>
#include <mutex>

/**
 * Core component: allocates tasks to edge nodes using multi-constraint algorithm
 * Can implement different allocation strategies
 */
class ResourceAllocator {
public:
    enum class AllocationStrategy {
        ROUND_ROBIN,
        LEAST_LOAD,
        PRIORITY_BASED,
        MULTI_CONSTRAINT
    };

    struct AllocationWeights {
        double cpu_weight = 0.25;
        double ram_weight = 0.20;
        double bandwidth_weight = 0.15;
        double latency_weight = 0.15;
        double queue_weight = 0.15;
        double priority_weight = 0.10;
    };

    struct AllocationResult {
        bool success;
        std::string edge_id;
        std::string reason;
        double score;  // Score used for allocation decision
    };

    // Constructor with monitor reference
    ResourceAllocator(std::shared_ptr<ResourceMonitor> monitor, AllocationStrategy strategy);

    // Allocate a task
    AllocationResult allocateTask(std::shared_ptr<Task> task);

    // Allocate with bounded back-pressure for transient resource exhaustion.
    // The task is counted once; internal retries are not reported as failures.
    AllocationResult allocateTaskWithRetry(
        std::shared_ptr<Task> task,
        std::size_t max_retries,
        std::chrono::milliseconds retry_delay);

    // Strategy switching
    void setStrategy(AllocationStrategy strategy);
    AllocationStrategy getStrategy() const;

    // Weights configuration (for multi-constraint)
    void setAllocationWeights(const AllocationWeights& weights) { weights_ = weights; }
    AllocationWeights getWeights() const { return weights_; }

    // Statistics
    uint64_t getTotalAllocations() const;
    uint64_t getSuccessfulAllocations() const;
    uint64_t getFailedAllocations() const;
    double getSuccessRate() const;

private:
    std::shared_ptr<ResourceMonitor> monitor_;
    AllocationStrategy strategy_;
    AllocationWeights weights_;
    uint64_t total_allocations_;
    uint64_t successful_allocations_;
    uint64_t failed_allocations_;
    size_t round_robin_index_;  // For round-robin strategy
    mutable std::mutex strategy_mutex_;

    // Strategy implementations
    AllocationResult allocateRoundRobin(std::shared_ptr<Task> task);
    AllocationResult allocateLeastLoad(std::shared_ptr<Task> task);
    AllocationResult allocatePriorityBased(std::shared_ptr<Task> task);
    AllocationResult allocateMultiConstraint(std::shared_ptr<Task> task);

    // Helper methods
    std::vector<std::shared_ptr<EdgeNode>> getViableEdges(std::shared_ptr<Task> task);
    double calculateAllocationScore(
        const std::shared_ptr<EdgeNode>& edge,
        const std::shared_ptr<Task>& task
    );
    double normalizeCpuScore(const std::shared_ptr<EdgeNode>& edge);
    double normalizeRamScore(const std::shared_ptr<EdgeNode>& edge);
    double normalizeBandwidthScore(const std::shared_ptr<EdgeNode>& edge);
    double normalizeLatencyScore(const std::shared_ptr<EdgeNode>& edge);
    double normalizeQueueScore(const std::shared_ptr<EdgeNode>& edge);
    double priorityScore(Task::TaskPriority priority);
};

#endif // RESOURCE_ALLOCATOR_H
