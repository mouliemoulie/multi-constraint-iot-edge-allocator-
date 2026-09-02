#include "resource/resource_allocator.h"
#include "common/constants.h"
#include "common/logger.h"
#include "database/database_manager.h"

#include <algorithm>
#include <limits>
#include <thread>

namespace {

const char* allocationStrategyName(ResourceAllocator::AllocationStrategy strategy) {
    switch (strategy) {
        case ResourceAllocator::AllocationStrategy::ROUND_ROBIN:
            return "ROUND_ROBIN";
        case ResourceAllocator::AllocationStrategy::LEAST_LOAD:
            return "LEAST_LOAD";
        case ResourceAllocator::AllocationStrategy::PRIORITY_BASED:
            return "PRIORITY_BASED";
        case ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT:
            return "MULTI_CONSTRAINT";
        default:
            return "UNKNOWN";
    }
}

}  // namespace

ResourceAllocator::ResourceAllocator(
    std::shared_ptr<ResourceMonitor> monitor,
    AllocationStrategy strategy)
    : monitor_(std::move(monitor)),
      strategy_(strategy),
      total_allocations_(0U),
      successful_allocations_(0U),
      failed_allocations_(0U),
      round_robin_index_(0U) {
}

void ResourceAllocator::setStrategy(AllocationStrategy strategy) {
    std::lock_guard<std::mutex> lock(strategy_mutex_);
    strategy_ = strategy;
}

ResourceAllocator::AllocationStrategy ResourceAllocator::getStrategy() const {
    std::lock_guard<std::mutex> lock(strategy_mutex_);
    return strategy_;
}

std::vector<std::shared_ptr<EdgeNode>> ResourceAllocator::getViableEdges(std::shared_ptr<Task> task) {
    std::vector<std::shared_ptr<EdgeNode>> viable;

    const Task::TaskRequirements requirements = task->getRequirements();

    for (const auto& entry : monitor_->getAllEdgeNodes()) {
        const std::shared_ptr<EdgeNode>& edge = entry.second;
        if (edge == nullptr || !edge->isAvailable()) {
            continue;
        }

        if (edge->canAllocate(requirements.cpu_percent, requirements.ram_mb, requirements.bandwidth_mbps)) {
            viable.push_back(edge);
        }
    }

    return viable;
}

ResourceAllocator::AllocationResult ResourceAllocator::allocateTask(std::shared_ptr<Task> task) {
    std::lock_guard<std::mutex> strategy_lock(strategy_mutex_);
    ++total_allocations_;

    AllocationResult result;
    result.success = false;
    result.score = 0.0;

    switch (strategy_) {
        case AllocationStrategy::ROUND_ROBIN:
            result = allocateRoundRobin(task);
            break;
        case AllocationStrategy::LEAST_LOAD:
            result = allocateLeastLoad(task);
            break;
        case AllocationStrategy::PRIORITY_BASED:
            result = allocatePriorityBased(task);
            break;
        case AllocationStrategy::MULTI_CONSTRAINT:
            result = allocateMultiConstraint(task);
            break;
        default:
            result.reason = "Unknown allocation strategy";
            break;
    }

    if (result.success) {
        ++successful_allocations_;
        task->setAssignedEdgeId(result.edge_id);
        task->setStatus(Task::TaskStatus::QUEUED);
        DatabaseManager::getInstance().updateTaskStatus(task->getTaskId(), Task::TaskStatus::QUEUED);
    } else {
        ++failed_allocations_;
        Logger::getInstance().warn("ResourceAllocator",
            "Failed to allocate task " + task->getTaskId() + ": " + result.reason);
    }

    return result;
}

// Allocate a task while applying back-pressure when resources are temporarily
// exhausted. This keeps the allocator honest: it never overcommits a node,
// but the simulation does not turn a short-lived busy condition into a
// permanent task failure.
ResourceAllocator::AllocationResult ResourceAllocator::allocateTaskWithRetry(
    std::shared_ptr<Task> task,
    std::size_t max_retries,
    std::chrono::milliseconds retry_delay) {

    std::lock_guard<std::mutex> strategy_lock(strategy_mutex_);

    AllocationResult result;
    result.success = false;
    result.score = 0.0;

    // Count this as one logical allocation request. The single-attempt
    // allocateTask() below is deliberately not used so retry attempts do not
    // inflate the public allocation statistics.
    ++total_allocations_;

    for (std::size_t attempt = 0U; attempt <= max_retries; ++attempt) {
        switch (strategy_) {
            case AllocationStrategy::ROUND_ROBIN:
                result = allocateRoundRobin(task);
                break;
            case AllocationStrategy::LEAST_LOAD:
                result = allocateLeastLoad(task);
                break;
            case AllocationStrategy::PRIORITY_BASED:
                result = allocatePriorityBased(task);
                break;
            case AllocationStrategy::MULTI_CONSTRAINT:
                result = allocateMultiConstraint(task);
                break;
            default:
                result.success = false;
                result.reason = "Unknown allocation strategy";
                break;
        }

        if (result.success) {
            ++successful_allocations_;
            task->setAssignedEdgeId(result.edge_id);
            task->setStatus(Task::TaskStatus::QUEUED);
            DatabaseManager::getInstance().updateTaskStatus(task->getTaskId(), Task::TaskStatus::QUEUED);
            Logger::getInstance().debug(
                "ResourceAllocator",
                "Task " + task->getTaskId() +
                " allocated to " + result.edge_id +
                " using " + allocationStrategyName(strategy_) +
                " (score=" + std::to_string(result.score) +
                ", reason=" + result.reason + ")");
            return result;
        }

        // No node exists at all: retrying cannot help. Likewise, an unknown
        // strategy is a configuration error, not transient congestion.
        if (result.reason == "Unknown allocation strategy" ||
            monitor_->getAllEdgeNodes().empty()) {
            break;
        }

        if (attempt < max_retries) {
            std::this_thread::sleep_for(retry_delay);
        }
    }

    ++failed_allocations_;
    Logger::getInstance().warn("ResourceAllocator",
        "Failed to allocate task " + task->getTaskId() + ": " + result.reason);
    return result;
}

// ============================================================
// Round Robin — spec §15, baseline comparison strategy
// ============================================================
ResourceAllocator::AllocationResult ResourceAllocator::allocateRoundRobin(std::shared_ptr<Task> task) {
    AllocationResult result;
    result.success = false;
    result.score = 0.0;

    const std::vector<std::shared_ptr<EdgeNode>> viable = getViableEdges(task);
    if (viable.empty()) {
        result.reason = "No viable edge node has sufficient resources";
        return result;
    }

    const std::shared_ptr<EdgeNode>& chosen = viable[round_robin_index_ % viable.size()];
    ++round_robin_index_;

    const Task::TaskRequirements requirements = task->getRequirements();
    chosen->allocateResources(requirements.cpu_percent, requirements.ram_mb, requirements.bandwidth_mbps);

    result.success = true;
    result.edge_id = chosen->getEdgeId();
    result.reason = "Round-robin selection";
    result.score = 0.0;  // Round robin does not compute a suitability score.
    return result;
}

// ============================================================
// Least Load — spec §15, baseline comparison strategy
// ============================================================
ResourceAllocator::AllocationResult ResourceAllocator::allocateLeastLoad(std::shared_ptr<Task> task) {
    AllocationResult result;
    result.success = false;
    result.score = 0.0;

    const std::vector<std::shared_ptr<EdgeNode>> viable = getViableEdges(task);
    if (viable.empty()) {
        result.reason = "No viable edge node has sufficient resources";
        return result;
    }

    std::shared_ptr<EdgeNode> best = nullptr;
    double lowest_cpu_usage = std::numeric_limits<double>::max();

    for (const auto& edge : viable) {
        const double used_cpu = edge->getCapacity().cpu_percent - edge->getAvailableCpuPercent();
        if (used_cpu < lowest_cpu_usage) {
            lowest_cpu_usage = used_cpu;
            best = edge;
        }
    }

    const Task::TaskRequirements requirements = task->getRequirements();
    best->allocateResources(requirements.cpu_percent, requirements.ram_mb, requirements.bandwidth_mbps);

    result.success = true;
    result.edge_id = best->getEdgeId();
    result.reason = "Lowest current CPU usage";
    result.score = lowest_cpu_usage;
    return result;
}

// ============================================================
// Priority Based — spec §15, baseline comparison strategy
// ============================================================
ResourceAllocator::AllocationResult ResourceAllocator::allocatePriorityBased(std::shared_ptr<Task> task) {
    AllocationResult result;
    result.success = false;
    result.score = 0.0;

    const std::vector<std::shared_ptr<EdgeNode>> viable = getViableEdges(task);
    if (viable.empty()) {
        result.reason = "No viable edge node has sufficient resources";
        return result;
    }

    // For CRITICAL/HIGH priority tasks, prefer the lowest-latency edge.
    // For LOW/MEDIUM, prefer least loaded — a simple but distinct policy
    // from pure least-load, giving the comparative experiment (spec §18)
    // a genuinely different strategy to measure against.
    const Task::TaskRequirements requirements = task->getRequirements();
    const bool is_high_priority =
        (requirements.priority == Task::TaskPriority::HIGH) ||
        (requirements.priority == Task::TaskPriority::CRITICAL);

    std::shared_ptr<EdgeNode> best = nullptr;

    if (is_high_priority) {
        int64_t lowest_latency = std::numeric_limits<int64_t>::max();
        for (const auto& edge : viable) {
            if (edge->getLatencyMs() < lowest_latency) {
                lowest_latency = edge->getLatencyMs();
                best = edge;
            }
        }
        result.reason = "High-priority task routed to lowest-latency edge";
        result.score = static_cast<double>(lowest_latency);
    } else {
        double lowest_queue = std::numeric_limits<double>::max();
        for (const auto& edge : viable) {
            const double queue_length = static_cast<double>(edge->getQueueLength());
            if (queue_length < lowest_queue) {
                lowest_queue = queue_length;
                best = edge;
            }
        }
        result.reason = "Standard-priority task routed to shortest queue";
        result.score = lowest_queue;
    }

    best->allocateResources(requirements.cpu_percent, requirements.ram_mb, requirements.bandwidth_mbps);

    result.success = true;
    result.edge_id = best->getEdgeId();
    return result;
}

// ============================================================
// Multi-Constraint — spec §3, §8, §15: the project's primary contribution
// ============================================================
ResourceAllocator::AllocationResult ResourceAllocator::allocateMultiConstraint(std::shared_ptr<Task> task) {
    AllocationResult result;
    result.success = false;
    result.score = 0.0;

    const std::vector<std::shared_ptr<EdgeNode>> viable = getViableEdges(task);
    if (viable.empty()) {
        result.reason = "No viable edge node has sufficient resources";
        return result;
    }

    std::shared_ptr<EdgeNode> best = nullptr;
    double best_score = -std::numeric_limits<double>::max();

    for (const auto& edge : viable) {
        const double score = calculateAllocationScore(edge, task);
        if (score > best_score) {
            best_score = score;
            best = edge;
        }
    }

    const Task::TaskRequirements requirements = task->getRequirements();
    best->allocateResources(requirements.cpu_percent, requirements.ram_mb, requirements.bandwidth_mbps);

    result.success = true;
    result.edge_id = best->getEdgeId();
    result.reason = "Highest multi-constraint suitability score";
    result.score = best_score;
    return result;
}

// ------------------------------------------------------------
// Spec §15 scoring formula:
//   Score = Wcpu*CPU_score + Wmem*Mem_score + Wbw*BW_score
//         + Wlat*Latency_score + Wq*Queue_score + Wpri*Priority_score
// Every *_score sub-term is normalized to [0, 1] before weighting so
// that no single raw-unit dimension (e.g. bandwidth in Mbps vs latency
// in ms) can dominate the sum purely due to its numeric scale.
// ------------------------------------------------------------
double ResourceAllocator::calculateAllocationScore(
    const std::shared_ptr<EdgeNode>& edge,
    const std::shared_ptr<Task>& task) {

    const double cpu_score = normalizeCpuScore(edge);
    const double ram_score = normalizeRamScore(edge);
    const double bandwidth_score = normalizeBandwidthScore(edge);
    const double latency_score = normalizeLatencyScore(edge);
    const double queue_score = normalizeQueueScore(edge);
    const double pri_score = priorityScore(task->getRequirements().priority);

    const double total_score =
        (weights_.cpu_weight * cpu_score) +
        (weights_.ram_weight * ram_score) +
        (weights_.bandwidth_weight * bandwidth_score) +
        (weights_.latency_weight * latency_score) +
        (weights_.queue_weight * queue_score) +
        (weights_.priority_weight * pri_score);

    return total_score * 100.0;  // Scale to a human-readable 0-100 range.
}

double ResourceAllocator::normalizeCpuScore(const std::shared_ptr<EdgeNode>& edge) {
    const double capacity = edge->getCapacity().cpu_percent;
    if (capacity <= 0.0) {
        return 0.0;
    }
    return std::clamp(edge->getAvailableCpuPercent() / capacity, 0.0, 1.0);
}

double ResourceAllocator::normalizeRamScore(const std::shared_ptr<EdgeNode>& edge) {
    const double capacity = edge->getCapacity().ram_mb;
    if (capacity <= 0.0) {
        return 0.0;
    }
    return std::clamp(edge->getAvailableRamMb() / capacity, 0.0, 1.0);
}

double ResourceAllocator::normalizeBandwidthScore(const std::shared_ptr<EdgeNode>& edge) {
    const double capacity = edge->getCapacity().bandwidth_mbps;
    if (capacity <= 0.0) {
        return 0.0;
    }
    return std::clamp(edge->getAvailableBandwidthMbps() / capacity, 0.0, 1.0);
}

double ResourceAllocator::normalizeLatencyScore(const std::shared_ptr<EdgeNode>& edge) {
    // Lower latency is better, so the score is inverted: a node at 0ms
    // scores 1.0, and score falls off toward 0 as latency grows. A
    // reference ceiling of 100ms is used for normalization; latencies
    // beyond that still clamp to a small positive floor rather than 0
    // so an otherwise-excellent node isn't zeroed out by latency alone.
    constexpr double kLatencyCeilingMs = 100.0;
    const double latency = static_cast<double>(edge->getLatencyMs());
    return std::clamp(1.0 - (latency / kLatencyCeilingMs), 0.01, 1.0);
}

double ResourceAllocator::normalizeQueueScore(const std::shared_ptr<EdgeNode>& edge) {
    const double queue_length = static_cast<double>(edge->getQueueLength());
    const double ceiling = static_cast<double>(constants::kMaxQueueLengthForScore);
    return std::clamp(1.0 - (queue_length / ceiling), 0.0, 1.0);
}

double ResourceAllocator::priorityScore(Task::TaskPriority priority) {
    double score = 0.0;
    switch (priority) {
        case Task::TaskPriority::CRITICAL:
            score = 1.0;
            break;
        case Task::TaskPriority::HIGH:
            score = 0.75;
            break;
        case Task::TaskPriority::MEDIUM:
            score = 0.5;
            break;
        case Task::TaskPriority::LOW:
            score = 0.25;
            break;
        default:
            score = 0.0;
            break;
    }
    return score;
}

uint64_t ResourceAllocator::getTotalAllocations() const {
    std::lock_guard<std::mutex> lock(strategy_mutex_);
    return total_allocations_;
}

uint64_t ResourceAllocator::getSuccessfulAllocations() const {
    std::lock_guard<std::mutex> lock(strategy_mutex_);
    return successful_allocations_;
}

uint64_t ResourceAllocator::getFailedAllocations() const {
    std::lock_guard<std::mutex> lock(strategy_mutex_);
    return failed_allocations_;
}

double ResourceAllocator::getSuccessRate() const {
    std::lock_guard<std::mutex> lock(strategy_mutex_);
    if (total_allocations_ == 0U) {
        return 0.0;
    }
    return (static_cast<double>(successful_allocations_) / static_cast<double>(total_allocations_)) * 100.0;
}
