#include <gtest/gtest.h>
#include "resource/resource_allocator.h"
#include "edge/resource_monitor.h"
#include "task/task.h"

namespace {

std::shared_ptr<EdgeNode> makeEdge(const std::string& id, double cpu_cap, int64_t latency_ms) {
    EdgeNode::ResourceCapacity cap;
    cap.cpu_percent = cpu_cap;
    cap.ram_mb = 16384.0;
    cap.bandwidth_mbps = 100.0;
    cap.latency_ms = latency_ms;
    return std::make_shared<EdgeNode>(id, cap);
}

std::shared_ptr<Task> makeTask(const std::string& id, Task::TaskPriority priority = Task::TaskPriority::HIGH) {
    Task::TaskRequirements req;
    req.cpu_percent = 10.0;
    req.ram_mb = 50.0;
    req.bandwidth_mbps = 1.0;
    req.deadline_ms = 1000;
    req.priority = priority;
    return std::make_shared<Task>(id, "DEV1", "ultrasonic", req, json::object());
}

class ResourceAllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        monitor = std::make_shared<ResourceMonitor>();
    }

    std::shared_ptr<ResourceMonitor> monitor;
};

}  // namespace

TEST_F(ResourceAllocatorTest, FailsGracefullyWithNoEdgeNodes) {
    ResourceAllocator allocator(monitor, ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT);
    auto task = makeTask("T1");

    const auto result = allocator.allocateTask(task);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(allocator.getTotalAllocations(), 1U);
    EXPECT_EQ(allocator.getFailedAllocations(), 1U);
}

TEST_F(ResourceAllocatorTest, MultiConstraintPrefersLeastLoadedNode) {
    // Spec §8 worked example: Edge C (35% CPU used) beats Edge A (75%
    // used) when both otherwise qualify.
    auto busy_edge = makeEdge("E01", 100.0, 10);
    busy_edge->allocateResources(75.0, 0.0, 0.0);  // 75% used, only 25% free

    auto free_edge = makeEdge("E03", 100.0, 8);
    free_edge->allocateResources(35.0, 0.0, 0.0);  // 35% used, 65% free

    monitor->registerEdgeNode(busy_edge);
    monitor->registerEdgeNode(free_edge);

    ResourceAllocator allocator(monitor, ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT);
    auto task = makeTask("T1045");

    const auto result = allocator.allocateTask(task);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.edge_id, "E03");
}

TEST_F(ResourceAllocatorTest, RoundRobinCyclesThroughViableEdges) {
    monitor->registerEdgeNode(makeEdge("E01", 100.0, 10));
    monitor->registerEdgeNode(makeEdge("E02", 100.0, 10));

    ResourceAllocator allocator(monitor, ResourceAllocator::AllocationStrategy::ROUND_ROBIN);

    std::vector<std::string> assigned_edges;
    for (int i = 0; i < 4; ++i) {
        auto task = makeTask("T" + std::to_string(i));
        const auto result = allocator.allocateTask(task);
        ASSERT_TRUE(result.success);
        assigned_edges.push_back(result.edge_id);
    }

    // With 2 edges and round-robin, we expect an alternating pattern.
    EXPECT_NE(assigned_edges[0], assigned_edges[1]);
}

TEST_F(ResourceAllocatorTest, LeastLoadPicksLowestCpuUsageEdge) {
    auto loaded = makeEdge("E01", 100.0, 10);
    loaded->allocateResources(80.0, 0.0, 0.0);

    auto idle = makeEdge("E02", 100.0, 10);
    idle->allocateResources(10.0, 0.0, 0.0);

    monitor->registerEdgeNode(loaded);
    monitor->registerEdgeNode(idle);

    ResourceAllocator allocator(monitor, ResourceAllocator::AllocationStrategy::LEAST_LOAD);
    const auto result = allocator.allocateTask(makeTask("T1"));

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.edge_id, "E02");
}

TEST_F(ResourceAllocatorTest, PriorityBasedRoutesCriticalTaskToLowestLatency) {
    monitor->registerEdgeNode(makeEdge("E_SLOW", 100.0, 50));
    monitor->registerEdgeNode(makeEdge("E_FAST", 100.0, 5));

    ResourceAllocator allocator(monitor, ResourceAllocator::AllocationStrategy::PRIORITY_BASED);
    auto critical_task = makeTask("T1", Task::TaskPriority::CRITICAL);

    const auto result = allocator.allocateTask(critical_task);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.edge_id, "E_FAST");
}

TEST_F(ResourceAllocatorTest, AllocationRespectsResourceCapacityLimits) {
    auto small_edge = makeEdge("E01", 5.0, 10);  // Only 5% CPU capacity total
    monitor->registerEdgeNode(small_edge);

    ResourceAllocator allocator(monitor, ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT);
    auto demanding_task = makeTask("T1");  // Requires 10% CPU

    const auto result = allocator.allocateTask(demanding_task);
    EXPECT_FALSE(result.success);
}

TEST_F(ResourceAllocatorTest, SuccessRateComputedCorrectly) {
    ResourceAllocator allocator(monitor, ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT);
    EXPECT_DOUBLE_EQ(allocator.getSuccessRate(), 0.0);  // No allocations yet.

    monitor->registerEdgeNode(makeEdge("E01", 100.0, 10));
    allocator.allocateTask(makeTask("T1"));  // succeeds
    // Second task fails because we deliberately exhaust capacity below.

    EXPECT_DOUBLE_EQ(allocator.getSuccessRate(), 100.0);
}

TEST_F(ResourceAllocatorTest, WeightsAreConfigurable) {
    ResourceAllocator allocator(monitor, ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT);

    ResourceAllocator::AllocationWeights custom_weights;
    custom_weights.cpu_weight = 1.0;
    custom_weights.ram_weight = 0.0;
    custom_weights.bandwidth_weight = 0.0;
    custom_weights.latency_weight = 0.0;
    custom_weights.queue_weight = 0.0;
    custom_weights.priority_weight = 0.0;

    allocator.setAllocationWeights(custom_weights);
    const auto retrieved = allocator.getWeights();
    EXPECT_DOUBLE_EQ(retrieved.cpu_weight, 1.0);
    EXPECT_DOUBLE_EQ(retrieved.ram_weight, 0.0);
}
