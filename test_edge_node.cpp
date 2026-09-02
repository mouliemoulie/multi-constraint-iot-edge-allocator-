#include <gtest/gtest.h>
#include "edge/edge_node.h"
#include "task/task.h"

namespace {

EdgeNode::ResourceCapacity makeCapacity() {
    EdgeNode::ResourceCapacity cap;
    cap.cpu_percent = 100.0;
    cap.ram_mb = 16384.0;
    cap.bandwidth_mbps = 100.0;
    cap.latency_ms = 10;
    return cap;
}

}  // namespace

TEST(EdgeNodeTest, InitialStateHasFullAvailableResources) {
    EdgeNode node("E01", makeCapacity());
    EXPECT_DOUBLE_EQ(node.getAvailableCpuPercent(), 100.0);
    EXPECT_DOUBLE_EQ(node.getAvailableRamMb(), 16384.0);
    EXPECT_TRUE(node.isAvailable());
    EXPECT_EQ(node.getQueueLength(), 0);
}

TEST(EdgeNodeTest, CanAllocateReflectsAvailableCapacity) {
    EdgeNode node("E01", makeCapacity());
    EXPECT_TRUE(node.canAllocate(50.0, 1000.0, 10.0));
    EXPECT_FALSE(node.canAllocate(150.0, 1000.0, 10.0));  // exceeds CPU capacity
}

TEST(EdgeNodeTest, AllocateResourcesReducesAvailability) {
    EdgeNode node("E01", makeCapacity());
    node.allocateResources(30.0, 500.0, 5.0);

    EXPECT_DOUBLE_EQ(node.getAvailableCpuPercent(), 70.0);
    EXPECT_DOUBLE_EQ(node.getAvailableRamMb(), 16384.0 - 500.0);
    EXPECT_DOUBLE_EQ(node.getAvailableBandwidthMbps(), 100.0 - 5.0);
}

TEST(EdgeNodeTest, DeallocateResourcesRestoresAvailability) {
    EdgeNode node("E01", makeCapacity());
    node.allocateResources(30.0, 500.0, 5.0);
    node.deallocateResources(30.0, 500.0, 5.0);

    EXPECT_DOUBLE_EQ(node.getAvailableCpuPercent(), 100.0);
    EXPECT_DOUBLE_EQ(node.getAvailableRamMb(), 16384.0);
}

TEST(EdgeNodeTest, DeallocateNeverGoesNegative) {
    EdgeNode node("E01", makeCapacity());
    // Deallocating more than was ever allocated must clamp at zero usage,
    // not underflow into a negative "used" value.
    node.deallocateResources(1000.0, 1000.0, 1000.0);
    EXPECT_DOUBLE_EQ(node.getAvailableCpuPercent(), 100.0);
}

TEST(EdgeNodeTest, QueueOperationsAreFifo) {
    EdgeNode node("E01", makeCapacity());

    Task::TaskRequirements req;
    req.cpu_percent = 1.0;
    req.ram_mb = 1.0;
    req.bandwidth_mbps = 1.0;
    req.deadline_ms = 100;
    req.priority = Task::TaskPriority::LOW;

    auto task1 = std::make_shared<Task>("T1", "DEV1", "temperature", req, json::object());
    auto task2 = std::make_shared<Task>("T2", "DEV1", "temperature", req, json::object());

    node.enqueueTask(task1);
    node.enqueueTask(task2);
    EXPECT_EQ(node.getQueueLength(), 2);

    const auto dequeued = node.dequeueTask();
    ASSERT_NE(dequeued, nullptr);
    EXPECT_EQ(dequeued->getTaskId(), "T1");
    EXPECT_EQ(node.getQueueLength(), 1);
}

TEST(EdgeNodeTest, DequeueOnEmptyQueueReturnsNull) {
    EdgeNode node("E01", makeCapacity());
    EXPECT_EQ(node.dequeueTask(), nullptr);
}

TEST(EdgeNodeTest, UnavailableNodeCannotAllocate) {
    EdgeNode node("E01", makeCapacity());
    node.setAvailable(false);
    EXPECT_FALSE(node.canAllocate(1.0, 1.0, 1.0));
}

TEST(EdgeNodeTest, ResourceFluctuationStaysWithinCapacity) {
    EdgeNode node("E01", makeCapacity());
    for (int i = 0; i < 50; ++i) {
        node.simulateResourceFluctuation();
        EXPECT_GE(node.getAvailableCpuPercent(), 0.0);
        EXPECT_LE(node.getAvailableCpuPercent(), 100.0);
    }
}
