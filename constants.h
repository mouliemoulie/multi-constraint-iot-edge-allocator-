#ifndef COMMON_CONSTANTS_H
#define COMMON_CONSTANTS_H

#include <cstdint>

/**
 * All simulation-wide constant values.
 * MISRA discipline: no magic numbers anywhere else in the codebase.
 * Every threshold, default, and limit is named here exactly once.
 */
namespace constants {

// ---- Resource bounds ----
constexpr double kMinCpuPercent = 0.0;
constexpr double kMaxCpuPercent = 100.0;
constexpr double kMinRamMb = 0.0;

// ---- Allocator defaults ----
constexpr double kDefaultCpuWeight = 0.25;
constexpr double kDefaultRamWeight = 0.20;
constexpr double kDefaultBandwidthWeight = 0.15;
constexpr double kDefaultLatencyWeight = 0.15;
constexpr double kDefaultQueueWeight = 0.15;
constexpr double kDefaultPriorityWeight = 0.10;

// ---- Queue / capacity limits ----
constexpr std::size_t kMaxQueueLengthForScore = 100U;
constexpr std::size_t kDefaultTaskQueueWarnThreshold = 50U;

// ---- Timing ----
constexpr int64_t kDefaultTaskDeadlineMs = 100;
constexpr int64_t kResourceMonitorPollIntervalMs = 500;
constexpr int64_t kSimulationTickIntervalMs = 100;

// ---- Simulation scale (spec §16: "100 devices / 10 edges / 10000 tasks") ----
constexpr std::size_t kDefaultDeviceCount = 100U;
constexpr std::size_t kDefaultEdgeNodeCount = 10U;
constexpr std::size_t kDefaultTaskCount = 10000U;

// ---- Retry / fault-tolerance ----
constexpr std::uint32_t kMaxReallocationAttempts = 3U;
constexpr int64_t kNodeFailureDetectionTimeoutMs = 1000;

// ---- Database ----
constexpr std::size_t kDefaultDbBatchSize = 100U;

}  // namespace constants

#endif  // COMMON_CONSTANTS_H
