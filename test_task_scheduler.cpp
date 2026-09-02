#include <gtest/gtest.h>
#include "scheduler/task_scheduler.h"
#include "edge/edge_node.h"

namespace {

std::shared_ptr<EdgeNode> makeEdge() {
    EdgeNode::ResourceCapacity cap;
    cap.cpu_percent = 100.0;
    cap.ram_mb = 16384.0;
    cap.bandwidth_mbps = 100.0;
    cap.latency_ms = 10;
    return std::make_shared<EdgeNode>("E01", cap);
}

std::shared_ptr<Task> makeTask(const std::string& id, Task::TaskPriority priority, int64_t deadline_ms) {
    Task::TaskRequirements req;
    req.cpu_percent = 1.0;
    req.ram_mb = 1.0;
    req.bandwidth_mbps = 1.0;
    req.deadline_ms = deadline_ms;
    req.priority = priority;
    return std::make_shared<Task>(id, "DEV1", "temperature", req, json::object());
}

}  // namespace

TEST(TaskSchedulerTest, FifoReturnsTasksInInsertionOrder) {
    TaskScheduler scheduler(TaskScheduler::SchedulingStrategy::FIFO);
    auto edge = makeEdge();

    scheduler.scheduleTask(edge, makeTask("T1", Task::TaskPriority::LOW, 1000));
    scheduler.scheduleTask(edge, makeTask("T2", Task::TaskPriority::CRITICAL, 1000));

    const auto first = scheduler.getNextTask(edge);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->getTaskId(), "T1");  // FIFO ignores priority
}

TEST(TaskSchedulerTest, PriorityBasedReturnsHighestPriorityFirst) {
    // Spec §13 example: T103(CRITICAL) -> T101(HIGH) -> T104(MEDIUM) -> T102(LOW)
    TaskScheduler scheduler(TaskScheduler::SchedulingStrategy::PRIORITY_BASED);
    auto edge = makeEdge();

    scheduler.scheduleTask(edge, makeTask("T101", Task::TaskPriority::HIGH, 1000));
    scheduler.scheduleTask(edge, makeTask("T102", Task::TaskPriority::LOW, 1000));
    scheduler.scheduleTask(edge, makeTask("T103", Task::TaskPriority::CRITICAL, 1000));
    scheduler.scheduleTask(edge, makeTask("T104", Task::TaskPriority::MEDIUM, 1000));

    EXPECT_EQ(scheduler.getNextTask(edge)->getTaskId(), "T103");
    EXPECT_EQ(scheduler.getNextTask(edge)->getTaskId(), "T101");
    EXPECT_EQ(scheduler.getNextTask(edge)->getTaskId(), "T104");
    EXPECT_EQ(scheduler.getNextTask(edge)->getTaskId(), "T102");
}

TEST(TaskSchedulerTest, EarliestDeadlineFirstOrdersByRemainingTime) {
    TaskScheduler scheduler(TaskScheduler::SchedulingStrategy::EARLIEST_DEADLINE_FIRST);
    auto edge = makeEdge();

    scheduler.scheduleTask(edge, makeTask("T_LONG", Task::TaskPriority::LOW, 5000));
    scheduler.scheduleTask(edge, makeTask("T_SHORT", Task::TaskPriority::LOW, 50));

    const auto first = scheduler.getNextTask(edge);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->getTaskId(), "T_SHORT");
}

TEST(TaskSchedulerTest, GetNextTaskOnEmptyQueueReturnsNull) {
    TaskScheduler scheduler(TaskScheduler::SchedulingStrategy::PRIORITY_BASED);
    auto edge = makeEdge();
    EXPECT_EQ(scheduler.getNextTask(edge), nullptr);
}

TEST(TaskSchedulerTest, RemainingTasksArePreservedAfterSelection) {
    TaskScheduler scheduler(TaskScheduler::SchedulingStrategy::PRIORITY_BASED);
    auto edge = makeEdge();

    scheduler.scheduleTask(edge, makeTask("T_LOW", Task::TaskPriority::LOW, 1000));
    scheduler.scheduleTask(edge, makeTask("T_HIGH", Task::TaskPriority::HIGH, 1000));

    scheduler.getNextTask(edge);  // Consumes T_HIGH
    EXPECT_EQ(edge->getQueueLength(), 1);  // T_LOW should remain

    const auto remaining = scheduler.getNextTask(edge);
    ASSERT_NE(remaining, nullptr);
    EXPECT_EQ(remaining->getTaskId(), "T_LOW");
}

TEST(TaskSchedulerTest, NullEdgeOrTaskIsHandledSafely) {
    TaskScheduler scheduler(TaskScheduler::SchedulingStrategy::FIFO);
    EXPECT_NO_THROW(scheduler.scheduleTask(nullptr, nullptr));
    EXPECT_EQ(scheduler.getNextTask(nullptr), nullptr);
}
