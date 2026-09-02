#ifndef METRICS_COLLECTOR_H
#define METRICS_COLLECTOR_H

#include "../resource/resource_allocator.h"
#include "../executor/task_executor.h"
#include "../edge/resource_monitor.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * Aggregates and reports the metrics named in spec §17:
 *   - Average latency
 *   - Waiting time
 *   - Deadline miss rate
 *   - CPU utilization
 *   - Load imbalance
 *   - Throughput
 *   - Task success rate
 *
 * This class only READS from the other components (allocator, executor,
 * monitor) — it owns no simulation state of its own, so it can be
 * constructed fresh for each experiment run described in spec §18
 * without needing to reset anything else.
 */
class MetricsCollector {
public:
    struct Snapshot {
        double average_latency_ms;
        double average_waiting_time_ms;
        double deadline_miss_rate_percent;
        double average_cpu_utilization_percent;
        double load_imbalance_stddev;
        double throughput_tasks_per_second;
        double task_success_rate_percent;
    };

    MetricsCollector(
        std::shared_ptr<ResourceAllocator> allocator,
        std::shared_ptr<TaskExecutor> executor,
        std::shared_ptr<ResourceMonitor> monitor);

    Snapshot collect(double elapsed_seconds) const;
    json collectAsJson(double elapsed_seconds) const;

    // Spec §18: side-by-side comparison table across strategies.
    static std::string formatComparisonTable(
        const std::vector<std::pair<std::string, Snapshot>>& results);

private:
    std::shared_ptr<ResourceAllocator> allocator_;
    std::shared_ptr<TaskExecutor> executor_;
    std::shared_ptr<ResourceMonitor> monitor_;

    double calculateLoadImbalance() const;
    double calculateAverageCpuUtilization() const;
};

#endif  // METRICS_COLLECTOR_H
