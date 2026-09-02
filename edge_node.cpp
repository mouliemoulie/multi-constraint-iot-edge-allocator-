#include "edge/edge_node.h"
#include "common/constants.h"

#include <algorithm>
#include <random>

EdgeNode::EdgeNode(const std::string& edge_id, const ResourceCapacity& capacity)
    : edge_id_(edge_id),
      capacity_(capacity),
      current_usage_{0.0, 0.0, 0.0},
      is_available_(true),
      total_tasks_processed_(0U),
      total_tasks_failed_(0U) {
}

EdgeNode::ResourceUsage EdgeNode::getCurrentUsage() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    return current_usage_;
}

double EdgeNode::getAvailableCpuPercent() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    return capacity_.cpu_percent - current_usage_.cpu_percent;
}

double EdgeNode::getAvailableRamMb() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    return capacity_.ram_mb - current_usage_.ram_mb;
}

double EdgeNode::getAvailableBandwidthMbps() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    return capacity_.bandwidth_mbps - current_usage_.bandwidth_mbps;
}

int64_t EdgeNode::getQueueLength() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    return static_cast<int64_t>(task_queue_.size());
}

bool EdgeNode::hasTasksInQueue() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    return !task_queue_.empty();
}

std::vector<std::shared_ptr<Task>> EdgeNode::getQueuedTasks() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    std::queue<std::shared_ptr<Task>> copy = task_queue_;
    std::vector<std::shared_ptr<Task>> result;
    result.reserve(copy.size());
    while (!copy.empty()) {
        result.push_back(copy.front());
        copy.pop();
    }
    return result;
}

void EdgeNode::enqueueTask(std::shared_ptr<Task> task) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    task_queue_.push(task);
}

std::shared_ptr<Task> EdgeNode::dequeueTask() {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (task_queue_.empty()) {
        return nullptr;
    }
    std::shared_ptr<Task> task = task_queue_.front();
    task_queue_.pop();
    return task;
}

bool EdgeNode::canAllocate(double cpu, double ram, double bandwidth) const {
    std::lock_guard<std::mutex> lock(resource_mutex_);

    const double available_cpu = capacity_.cpu_percent - current_usage_.cpu_percent;
    const double available_ram = capacity_.ram_mb - current_usage_.ram_mb;
    const double available_bandwidth = capacity_.bandwidth_mbps - current_usage_.bandwidth_mbps;

    return is_available_ &&
           (available_cpu >= cpu) &&
           (available_ram >= ram) &&
           (available_bandwidth >= bandwidth);
}

void EdgeNode::allocateResources(double cpu, double ram, double bandwidth) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    current_usage_.cpu_percent += cpu;
    current_usage_.ram_mb += ram;
    current_usage_.bandwidth_mbps += bandwidth;
    ++total_tasks_processed_;
}

void EdgeNode::deallocateResources(double cpu, double ram, double bandwidth) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    current_usage_.cpu_percent = std::max(constants::kMinCpuPercent, current_usage_.cpu_percent - cpu);
    current_usage_.ram_mb = std::max(constants::kMinRamMb, current_usage_.ram_mb - ram);
    current_usage_.bandwidth_mbps = std::max(0.0, current_usage_.bandwidth_mbps - bandwidth);
}

void EdgeNode::simulateResourceFluctuation() {
    // Spec §7: "The simulator continuously changes their resource
    // usage" — models background/system load independent of tasks this
    // allocator has assigned, e.g. E01: "CPU: 40% -> 55% -> 75% -> 90%".
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_real_distribution<double> fluctuation(-5.0, 5.0);

    std::lock_guard<std::mutex> lock(resource_mutex_);

    current_usage_.cpu_percent += fluctuation(generator);
    current_usage_.cpu_percent = std::clamp(
        current_usage_.cpu_percent, constants::kMinCpuPercent, capacity_.cpu_percent);

    current_usage_.ram_mb += fluctuation(generator) * (capacity_.ram_mb / 100.0);
    current_usage_.ram_mb = std::clamp(
        current_usage_.ram_mb, constants::kMinRamMb, capacity_.ram_mb);
}
