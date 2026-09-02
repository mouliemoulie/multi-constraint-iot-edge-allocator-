#ifndef LIVE_RUNTIME_H
#define LIVE_RUNTIME_H

#include "simulation_config.h"
#include "resource/resource_allocator.h"
#include "scheduler/task_scheduler.h"
#include "metrics/metrics_collector.h"
#include "iot_device/iot_device.h"
#include "edge/resource_monitor.h"
#include "task/task_manager.h"
#include "executor/task_executor.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class LiveRuntime {
public:
    LiveRuntime();
    ~LiveRuntime();

    bool initialize(const SimulationConfig& config);
    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }
    bool isProcessingComplete() const { return processing_complete_.load(); }

    bool applyConfiguration(bool automatic,
                            const std::string& allocation_strategy,
                            const std::string& scheduling_algorithm,
                            std::string& error);

    json configuration() const;
    json status() const;
    json devices() const;
    json edgeNodes() const;
    json tasks(std::size_t limit = 500) const;
    json activeTasks() const;
    json allocations() const;
    json metrics() const;
    json activities(std::size_t limit = 50) const;
    json alerts() const;
    json resourceHistory(std::size_t limit = 120) const;
    json taskLifecycle(const std::string& task_id) const;

private:
    SimulationConfig config_{};
    std::shared_ptr<ResourceMonitor> monitor_;
    std::shared_ptr<TaskManager> task_manager_;
    std::shared_ptr<ResourceAllocator> allocator_;
    std::shared_ptr<TaskScheduler> scheduler_;
    std::shared_ptr<TaskExecutor> executor_;
    std::vector<std::shared_ptr<EdgeNode>> edges_;
    std::vector<std::shared_ptr<IoTDevice>> devices_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> processing_complete_{false};
    std::atomic<std::size_t> generated_tasks_{0U};
    std::thread processing_thread_;
    mutable std::mutex configuration_mutex_;
    std::chrono::steady_clock::time_point start_time_{};

    void processingLoop();
    void recordResourceSnapshots();
    void chooseRecommendedConfiguration();

    static ResourceAllocator::AllocationStrategy allocationFromString(const std::string& value, bool& valid);
    static TaskScheduler::SchedulingStrategy schedulingFromString(const std::string& value, bool& valid);
    static std::string allocationToString(ResourceAllocator::AllocationStrategy strategy);
    static std::string allocationLabel(ResourceAllocator::AllocationStrategy strategy);
    static std::string schedulingToString(TaskScheduler::SchedulingStrategy strategy);
    static std::string schedulingLabel(TaskScheduler::SchedulingStrategy strategy);
};

#endif
