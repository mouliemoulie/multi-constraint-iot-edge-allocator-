#ifndef RESOURCE_CONTROLLER_H
#define RESOURCE_CONTROLLER_H

#include "../task/task_manager.h"
#include "../edge/resource_monitor.h"
#include "resource_allocator.h"
#include <memory>
#include <thread>
#include <atomic>

/**
 * Resource Controller orchestrates all resource allocation
 * Handles task distribution, failure detection, and reallocation
 */
class ResourceController {
public:
    ResourceController(
        std::shared_ptr<TaskManager> task_manager,
        std::shared_ptr<ResourceMonitor> resource_monitor,
        std::shared_ptr<ResourceAllocator> allocator
    );

    ~ResourceController();

    // Start/stop processing
    void start();
    void stop();
    bool isRunning() const { return is_running_; }

    // Configuration
    void setAllocationStrategy(ResourceAllocator::AllocationStrategy strategy);
    void setAllocationWeights(const ResourceAllocator::AllocationWeights& weights);

    // Manual task allocation
    ResourceAllocator::AllocationResult allocateTaskNow(std::shared_ptr<Task> task);

    // Failure handling
    void handleNodeFailure(const std::string& edge_id);
    void reallocateTasksFromFailedNode(const std::string& edge_id);

    // Statistics
    uint64_t getTotalTasksAllocated() const { return total_tasks_allocated_; }
    uint64_t getTotalAllocationFailures() const { return total_allocation_failures_; }
    uint64_t getTotalReallocations() const { return total_reallocations_; }

private:
    std::shared_ptr<TaskManager> task_manager_;
    std::shared_ptr<ResourceMonitor> resource_monitor_;
    std::shared_ptr<ResourceAllocator> allocator_;
    std::atomic<bool> is_running_;
    std::thread processing_thread_;
    uint64_t total_tasks_allocated_;
    uint64_t total_allocation_failures_;
    uint64_t total_reallocations_;

    // Main processing loop
    void processingLoop();

    // Callbacks for task manager and monitor
    void onTaskCreated(std::shared_ptr<Task> task);
    void onNodeStatusChanged(const std::string& edge_id, bool is_available);
};

#endif // RESOURCE_CONTROLLER_H
