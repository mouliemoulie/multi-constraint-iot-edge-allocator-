#include <gtest/gtest.h>
#include "metrics/metrics_collector.h"
#include "resource/resource_allocator.h"
#include "executor/task_executor.h"
#include "edge/resource_monitor.h"
#include "scheduler/task_scheduler.h"

namespace {

std::shared_ptr<EdgeNode> makeEdge(const std::string& id) {
    EdgeNode::ResourceCapacity cap;
    cap.cpu_percent = 100.0;
    cap.ram_mb = 16384.0;
    cap.bandwidth_mbps = 100.0;
    cap.latency_ms = 15;
    return std::make_shared<EdgeNode>(id, cap);
}

}  // namespace

class MetricsCollectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        monitor = std::make_shared<ResourceMonitor>();
        monitor->registerEdgeNode(makeEdge("E01"));
        monitor->registerEdgeNode(makeEdge("E02"));

        allocator = std::make_shared<ResourceAllocator>(
            monitor, ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT);

        auto scheduler = std::make_shared<TaskScheduler>(TaskScheduler::SchedulingStrategy::FIFO);
        executor = std::make_shared<TaskExecutor>(scheduler);
    }

    std::shared_ptr<ResourceMonitor> monitor;
    std::shared_ptr<ResourceAllocator> allocator;
    std::shared_ptr<TaskExecutor> executor;
};

TEST_F(MetricsCollectorTest, CollectReturnsZeroedSnapshotWithNoActivity) {
    MetricsCollector collector(allocator, executor, monitor);
    const auto snapshot = collector.collect(1.0);

    EXPECT_DOUBLE_EQ(snapshot.throughput_tasks_per_second, 0.0);
    EXPECT_DOUBLE_EQ(snapshot.deadline_miss_rate_percent, 0.0);
}

TEST_F(MetricsCollectorTest, JsonSerializationContainsAllSpecMetrics) {
    // Spec §17: latency, waiting time, deadline miss, CPU utilization,
    // load imbalance, throughput, task success rate — all 7 required.
    MetricsCollector collector(allocator, executor, monitor);
    const json result = collector.collectAsJson(1.0);

    EXPECT_TRUE(result.contains("average_latency_ms"));
    EXPECT_TRUE(result.contains("average_waiting_time_ms"));
    EXPECT_TRUE(result.contains("deadline_miss_rate_percent"));
    EXPECT_TRUE(result.contains("average_cpu_utilization_percent"));
    EXPECT_TRUE(result.contains("load_imbalance_stddev"));
    EXPECT_TRUE(result.contains("throughput_tasks_per_second"));
    EXPECT_TRUE(result.contains("task_success_rate_percent"));
}

TEST_F(MetricsCollectorTest, ComparisonTableIncludesAllStrategyNames) {
    MetricsCollector::Snapshot snapshot{};
    std::vector<std::pair<std::string, MetricsCollector::Snapshot>> results = {
        {"Round Robin", snapshot},
        {"Multi-Constraint", snapshot}
    };

    const std::string table = MetricsCollector::formatComparisonTable(results);
    EXPECT_NE(table.find("Round Robin"), std::string::npos);
    EXPECT_NE(table.find("Multi-Constraint"), std::string::npos);
}

TEST_F(MetricsCollectorTest, ZeroElapsedTimeDoesNotDivideByZero) {
    MetricsCollector collector(allocator, executor, monitor);
    EXPECT_NO_THROW({
        const auto snapshot = collector.collect(0.0);
        EXPECT_DOUBLE_EQ(snapshot.throughput_tasks_per_second, 0.0);
    });
}
