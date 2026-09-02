#include "metrics/metrics_collector.h"
#include "common/logger.h"

#include <cmath>
#include <sstream>
#include <iomanip>

MetricsCollector::MetricsCollector(
    std::shared_ptr<ResourceAllocator> allocator,
    std::shared_ptr<TaskExecutor> executor,
    std::shared_ptr<ResourceMonitor> monitor)
    : allocator_(std::move(allocator)),
      executor_(std::move(executor)),
      monitor_(std::move(monitor)) {
}

double MetricsCollector::calculateAverageCpuUtilization() const {
    const auto& nodes = monitor_->getAllEdgeNodes();
    if (nodes.empty()) {
        return 0.0;
    }

    double total_utilization = 0.0;
    std::size_t counted = 0U;

    for (const auto& entry : nodes) {
        const std::shared_ptr<EdgeNode>& node = entry.second;
        if (node == nullptr) {
            continue;
        }
        const EdgeNode::ResourceCapacity capacity = node->getCapacity();
        const EdgeNode::ResourceUsage usage = node->getCurrentUsage();

        if (capacity.cpu_percent > 0.0) {
            total_utilization += (usage.cpu_percent / capacity.cpu_percent) * 100.0;
            ++counted;
        }
    }

    if (counted == 0U) {
        return 0.0;
    }
    return total_utilization / static_cast<double>(counted);
}

double MetricsCollector::calculateLoadImbalance() const {
    // Standard deviation of per-node CPU utilization percentages.
    // Low stddev = well-balanced load; high stddev = some nodes
    // overloaded while others sit idle (spec §17: "Load imbalance").
    const auto& nodes = monitor_->getAllEdgeNodes();
    if (nodes.empty()) {
        return 0.0;
    }

    std::vector<double> utilizations;
    utilizations.reserve(nodes.size());

    for (const auto& entry : nodes) {
        const std::shared_ptr<EdgeNode>& node = entry.second;
        if (node == nullptr) {
            continue;
        }
        const EdgeNode::ResourceCapacity capacity = node->getCapacity();
        const EdgeNode::ResourceUsage usage = node->getCurrentUsage();

        if (capacity.cpu_percent > 0.0) {
            utilizations.push_back((usage.cpu_percent / capacity.cpu_percent) * 100.0);
        }
    }

    if (utilizations.size() < 2U) {
        return 0.0;
    }

    double sum = 0.0;
    for (const double value : utilizations) {
        sum += value;
    }
    const double mean = sum / static_cast<double>(utilizations.size());

    double squared_diff_sum = 0.0;
    for (const double value : utilizations) {
        const double diff = value - mean;
        squared_diff_sum += diff * diff;
    }
    const double variance = squared_diff_sum / static_cast<double>(utilizations.size());

    return std::sqrt(variance);
}

MetricsCollector::Snapshot MetricsCollector::collect(double elapsed_seconds) const {
    Snapshot snapshot{};

    snapshot.average_latency_ms = executor_->getAverageLatency();
    snapshot.average_waiting_time_ms = executor_->getAverageExecutionTimeMs();
    snapshot.average_cpu_utilization_percent = calculateAverageCpuUtilization();
    snapshot.load_imbalance_stddev = calculateLoadImbalance();

    const uint64_t executed = executor_->getTotalTasksExecuted();
    const uint64_t failed = executor_->getTotalTasksFailed();
    const uint64_t completed_total = executed + failed;

    snapshot.deadline_miss_rate_percent = (completed_total > 0U)
        ? (static_cast<double>(failed) / static_cast<double>(completed_total)) * 100.0
        : 0.0;

    snapshot.task_success_rate_percent = allocator_->getSuccessRate();

    snapshot.throughput_tasks_per_second = (elapsed_seconds > 0.0)
        ? static_cast<double>(executed) / elapsed_seconds
        : 0.0;

    return snapshot;
}

json MetricsCollector::collectAsJson(double elapsed_seconds) const {
    const Snapshot snapshot = collect(elapsed_seconds);

    json result;
    result["average_latency_ms"] = snapshot.average_latency_ms;
    result["average_waiting_time_ms"] = snapshot.average_waiting_time_ms;
    result["deadline_miss_rate_percent"] = snapshot.deadline_miss_rate_percent;
    result["average_cpu_utilization_percent"] = snapshot.average_cpu_utilization_percent;
    result["load_imbalance_stddev"] = snapshot.load_imbalance_stddev;
    result["throughput_tasks_per_second"] = snapshot.throughput_tasks_per_second;
    result["task_success_rate_percent"] = snapshot.task_success_rate_percent;
    return result;
}

std::string MetricsCollector::formatComparisonTable(
    const std::vector<std::pair<std::string, Snapshot>>& results) {

    // Spec §18: side-by-side table comparing Round Robin / Least Load /
    // Priority Based / Multi-Constraint across Latency, Deadline Miss,
    // CPU Utilization, Load Imbalance.
    std::ostringstream table;

    table << std::left
          << std::setw(18) << "Algorithm"
          << std::setw(14) << "Latency(ms)"
          << std::setw(16) << "DeadlineMiss(%)"
          << std::setw(16) << "CPUUtil(%)"
          << std::setw(16) << "LoadImbalance"
          << std::setw(14) << "Throughput"
          << std::setw(14) << "Success(%)"
          << "\n";

    table << std::string(108, '-') << "\n";

    for (const auto& entry : results) {
        const std::string& name = entry.first;
        const Snapshot& s = entry.second;

        table << std::left << std::setw(18) << name
              << std::fixed << std::setprecision(2)
              << std::setw(14) << s.average_waiting_time_ms
              << std::setw(16) << s.deadline_miss_rate_percent
              << std::setw(16) << s.average_cpu_utilization_percent
              << std::setw(16) << s.load_imbalance_stddev
              << std::setw(14) << s.throughput_tasks_per_second
              << std::setw(14) << s.task_success_rate_percent
              << "\n";
    }

    return table.str();
}
