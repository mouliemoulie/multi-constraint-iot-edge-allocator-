#include "resource/resource_controller.h"
#include "common/constants.h"
#include "common/logger.h"

#include <chrono>

ResourceController::ResourceController(
    std::shared_ptr<TaskManager> task_manager,
    std::shared_ptr<ResourceMonitor> resource_monitor,
    std::shared_ptr<ResourceAllocator> allocator)
    : task_manager_(std::move(task_manager)),
      resource_monitor_(std::move(resource_monitor)),
      allocator_(std::move(allocator)),
      is_running_(false),
      total_tasks_allocated_(0U),
      total_allocation_failures_(0U),
      total_reallocations_(0U) {

    // Spec §14: Task Manager -> Task -> Resource Controller. Wiring the
    // callback here means any task the TaskManager creates is
    // automatically forwarded for allocation without the caller having
    // to manually pump a queue.
    task_manager_->registerTaskCreationCallback(
        [this](std::shared_ptr<Task> task) { onTaskCreated(task); });

    resource_monitor_->registerStatusCallback(
        [this](const std::string& edge_id, bool available) {
            onNodeStatusChanged(edge_id, available);
        });
}

ResourceController::~ResourceController() {
    stop();
}

void ResourceController::start() {
    if (is_running_) {
        return;
    }
    is_running_ = true;
    processing_thread_ = std::thread(&ResourceController::processingLoop, this);
    Logger::getInstance().info("ResourceController", "Started.");
}

void ResourceController::stop() {
    if (!is_running_) {
        return;
    }
    is_running_ = false;
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
    Logger::getInstance().info("ResourceController", "Stopped.");
}

void ResourceController::processingLoop() {
    while (is_running_) {
        if (task_manager_->hasTasksInQueue()) {
            std::shared_ptr<Task> task = task_manager_->dequeueTask();
            if (task != nullptr) {
                allocateTaskNow(task);
            }
        }

        resource_monitor_->updateResourceMetrics();

        std::this_thread::sleep_for(
            std::chrono::milliseconds(constants::kResourceMonitorPollIntervalMs));
    }
}

void ResourceController::onTaskCreated(std::shared_ptr<Task> task) {
    task_manager_->enqueueTask(task);
}

void ResourceController::onNodeStatusChanged(const std::string& edge_id, bool is_available) {
    if (!is_available) {
        handleNodeFailure(edge_id);
    }
}

ResourceAllocator::AllocationResult ResourceController::allocateTaskNow(std::shared_ptr<Task> task) {
    const ResourceAllocator::AllocationResult result = allocator_->allocateTask(task);

    if (result.success) {
        ++total_tasks_allocated_;
    } else {
        ++total_allocation_failures_;
    }

    return result;
}

void ResourceController::setAllocationStrategy(ResourceAllocator::AllocationStrategy strategy) {
    allocator_->setStrategy(strategy);
}

void ResourceController::setAllocationWeights(const ResourceAllocator::AllocationWeights& weights) {
    allocator_->setAllocationWeights(weights);
}

void ResourceController::handleNodeFailure(const std::string& edge_id) {
    Logger::getInstance().warn("ResourceController",
        "Handling failure of edge node " + edge_id);
    reallocateTasksFromFailedNode(edge_id);
}

void ResourceController::reallocateTasksFromFailedNode(const std::string& edge_id) {
    // Spec §10: "Find tasks assigned to E03 -> Reallocation -> E01/E02".
    std::shared_ptr<EdgeNode> failed_node = resource_monitor_->getEdgeNode(edge_id);
    if (failed_node == nullptr) {
        return;
    }

    std::size_t reallocated_count = 0U;

    while (failed_node->hasTasksInQueue()) {
        std::shared_ptr<Task> task = failed_node->dequeueTask();
        if (task == nullptr) {
            continue;
        }

        task->setStatus(Task::TaskStatus::REALLOCATED);
        task->setAssignedEdgeId("");

        const ResourceAllocator::AllocationResult result = allocateTaskNow(task);
        if (result.success) {
            ++reallocated_count;
            ++total_reallocations_;
        } else {
            Logger::getInstance().error("ResourceController",
                "Could not reallocate task " + task->getTaskId() +
                " after node " + edge_id + " failure: " + result.reason);
        }
    }

    Logger::getInstance().info("ResourceController",
        "Reallocated " + std::to_string(reallocated_count) +
        " task(s) from failed node " + edge_id);
}
