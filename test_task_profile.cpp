#include <gtest/gtest.h>
#include "task/task_profile.h"

// TaskProfile is a process-wide singleton (see docs/MISRA_DEVIATIONS.md
// D6), so these tests only assert properties that hold regardless of
// what other tests may have added to it, rather than assuming a fresh
// instance per test.

TEST(TaskProfileTest, GetInstanceReturnsSameObject) {
    TaskProfile& first = TaskProfile::getInstance();
    TaskProfile& second = TaskProfile::getInstance();
    EXPECT_EQ(&first, &second);
}

TEST(TaskProfileTest, AddedProfileIsRetrievable) {
    Task::TaskRequirements req;
    req.cpu_percent = 42.0;
    req.ram_mb = 123.0;
    req.bandwidth_mbps = 2.5;
    req.deadline_ms = 250;
    req.priority = Task::TaskPriority::MEDIUM;

    TaskProfile::getInstance().addProfile("test_sensor_type", req);

    ASSERT_TRUE(TaskProfile::getInstance().hasProfile("test_sensor_type"));

    const Task::TaskRequirements retrieved =
        TaskProfile::getInstance().getRequirements("test_sensor_type");

    EXPECT_DOUBLE_EQ(retrieved.cpu_percent, 42.0);
    EXPECT_DOUBLE_EQ(retrieved.ram_mb, 123.0);
    EXPECT_DOUBLE_EQ(retrieved.bandwidth_mbps, 2.5);
    EXPECT_EQ(retrieved.deadline_ms, 250);
    EXPECT_EQ(retrieved.priority, Task::TaskPriority::MEDIUM);
}

TEST(TaskProfileTest, UnknownSensorTypeReturnsConservativeFallback) {
    ASSERT_FALSE(TaskProfile::getInstance().hasProfile("nonexistent_sensor_xyz"));

    const Task::TaskRequirements fallback =
        TaskProfile::getInstance().getRequirements("nonexistent_sensor_xyz");

    // Fallback should never crash and should be a "safe" low-priority
    // profile rather than zeros or negative values.
    EXPECT_GT(fallback.cpu_percent, 0.0);
    EXPECT_GT(fallback.ram_mb, 0.0);
    EXPECT_EQ(fallback.priority, Task::TaskPriority::LOW);
}

TEST(TaskProfileTest, DefaultProfilesMatchSpecValues) {
    // Values from project spec §6. loadProfiles() with a nonexistent DB
    // path falls back to these exact built-in defaults.
    TaskProfile::getInstance().loadProfiles("/nonexistent/path/does_not_exist.db");

    ASSERT_TRUE(TaskProfile::getInstance().hasProfile("ultrasonic"));
    const Task::TaskRequirements ultrasonic = TaskProfile::getInstance().getRequirements("ultrasonic");
    EXPECT_DOUBLE_EQ(ultrasonic.cpu_percent, 10.0);
    EXPECT_DOUBLE_EQ(ultrasonic.ram_mb, 50.0);
    EXPECT_DOUBLE_EQ(ultrasonic.bandwidth_mbps, 1.0);
    EXPECT_EQ(ultrasonic.deadline_ms, 100);

    ASSERT_TRUE(TaskProfile::getInstance().hasProfile("camera"));
    const Task::TaskRequirements camera = TaskProfile::getInstance().getRequirements("camera");
    EXPECT_DOUBLE_EQ(camera.cpu_percent, 60.0);
    EXPECT_DOUBLE_EQ(camera.ram_mb, 1024.0);
    EXPECT_EQ(camera.deadline_ms, 50);
}
