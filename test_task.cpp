#include <gtest/gtest.h>
#include "task/task.h"

#include <thread>
#include <chrono>

namespace {

Task::TaskRequirements makeRequirements(int64_t deadline_ms) {
    Task::TaskRequirements req;
    req.cpu_percent = 10.0;
    req.ram_mb = 50.0;
    req.bandwidth_mbps = 1.0;
    req.deadline_ms = deadline_ms;
    req.priority = Task::TaskPriority::HIGH;
    return req;
}

}  // namespace

TEST(TaskTest, ConstructorSetsFieldsCorrectly) {
    const json sensor_data = {{"distance_cm", 35.2}};
    Task task("T001", "US001", "ultrasonic", makeRequirements(100), sensor_data);

    EXPECT_EQ(task.getTaskId(), "T001");
    EXPECT_EQ(task.getDeviceId(), "US001");
    EXPECT_EQ(task.getSensorType(), "ultrasonic");
    EXPECT_EQ(task.getStatus(), Task::TaskStatus::CREATED);
    EXPECT_EQ(task.getAssignedEdgeId(), "");
}

TEST(TaskTest, DeadlineNotExceededImmediatelyAfterCreation) {
    Task task("T002", "US001", "ultrasonic", makeRequirements(10000), json::object());
    EXPECT_FALSE(task.isDeadlineExceeded());
}

TEST(TaskTest, DeadlineExceededAfterShortTimeout) {
    Task task("T003", "US001", "ultrasonic", makeRequirements(1), json::object());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_TRUE(task.isDeadlineExceeded());
}

TEST(TaskTest, StatusTransitionsPersist) {
    Task task("T004", "US001", "ultrasonic", makeRequirements(100), json::object());
    task.setStatus(Task::TaskStatus::QUEUED);
    EXPECT_EQ(task.getStatus(), Task::TaskStatus::QUEUED);

    task.setAssignedEdgeId("E01");
    EXPECT_EQ(task.getAssignedEdgeId(), "E01");
}

TEST(TaskTest, JsonRoundTripPreservesRequirements) {
    Task original("T005", "CAM001", "camera", makeRequirements(50), json{{"frame_number", 1}});
    original.setAssignedEdgeId("E02");
    original.setStatus(Task::TaskStatus::COMPLETED);

    const json serialized = original.toJson();
    const Task restored = Task::fromJson(serialized);

    EXPECT_EQ(restored.getTaskId(), original.getTaskId());
    EXPECT_EQ(restored.getDeviceId(), original.getDeviceId());
    EXPECT_EQ(restored.getSensorType(), original.getSensorType());
    EXPECT_EQ(restored.getAssignedEdgeId(), original.getAssignedEdgeId());
    EXPECT_EQ(restored.getStatus(), original.getStatus());
    EXPECT_DOUBLE_EQ(restored.getRequirements().cpu_percent, original.getRequirements().cpu_percent);
}

TEST(TaskTest, PriorityOrderingMatchesSpecExample) {
    // Spec §13: T103(CRITICAL) > T101(HIGH) > T104(MEDIUM) > T102(LOW)
    EXPECT_LT(Task::TaskPriority::LOW, Task::TaskPriority::MEDIUM);
    EXPECT_LT(Task::TaskPriority::MEDIUM, Task::TaskPriority::HIGH);
    EXPECT_LT(Task::TaskPriority::HIGH, Task::TaskPriority::CRITICAL);
}
