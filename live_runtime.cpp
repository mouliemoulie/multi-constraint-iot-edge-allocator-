#include "api/live_runtime.h"
#include "database/database_manager.h"
#include "task/task_profile.h"
#include "iot_device/sensor_simulator.h"
#include "common/logger.h"
#include <algorithm>
#include <iomanip>
#include <random>
#include <sstream>

namespace {

std::vector<std::shared_ptr<EdgeNode>> makeEdges(std::size_t count, const std::shared_ptr<ResourceMonitor>& monitor) {
    std::vector<std::shared_ptr<EdgeNode>> nodes;
    nodes.reserve(count);
    static thread_local std::mt19937 generator(42U);
    std::uniform_real_distribution<double> ram_dist(4096.0, 32768.0);
    std::uniform_real_distribution<double> bandwidth_dist(50.0, 200.0);
    std::uniform_int_distribution<int64_t> latency_dist(8, 40);

    for (std::size_t i = 0U; i < count; ++i) {
        std::ostringstream id;
        id << "E" << std::setfill('0') << std::setw(2) << (i + 1U);
        EdgeNode::ResourceCapacity capacity{};
        capacity.cpu_percent = 100.0;
        capacity.ram_mb = ram_dist(generator);
        capacity.bandwidth_mbps = bandwidth_dist(generator);
        capacity.latency_ms = latency_dist(generator);
        auto node = std::make_shared<EdgeNode>(id.str(), capacity);
        monitor->registerEdgeNode(node);
        DatabaseManager::getInstance().saveEdgeNode(node);
        nodes.push_back(node);
    }
    return nodes;
}

std::vector<std::shared_ptr<IoTDevice>> makeDevices(std::size_t count) {
    std::vector<std::shared_ptr<IoTDevice>> devices;
    devices.reserve(count);
    static const std::vector<IoTDevice::DeviceType> types = {
        IoTDevice::DeviceType::ULTRASONIC,
        IoTDevice::DeviceType::TEMPERATURE,
        IoTDevice::DeviceType::CAMERA,
        IoTDevice::DeviceType::HUMIDITY,
        IoTDevice::DeviceType::PRESSURE};

    for (std::size_t i = 0U; i < count; ++i) {
        std::ostringstream id;
        id << "DEV" << std::setfill('0') << std::setw(4) << (i + 1U);
        IoTDevice::DeviceConfig cfg{};
        cfg.device_id = id.str();
        cfg.type = types[i % types.size()];
        cfg.data_generation_interval_ms = 200.0;
        cfg.data_variance = 0.05;

        std::shared_ptr<IoTDevice> device;
        switch (cfg.type) {
            case IoTDevice::DeviceType::ULTRASONIC: device = std::make_shared<UltrasonicSensor>(cfg); break;
            case IoTDevice::DeviceType::TEMPERATURE: device = std::make_shared<TemperatureSensor>(cfg); break;
            case IoTDevice::DeviceType::CAMERA: device = std::make_shared<CameraSensor>(cfg); break;
            case IoTDevice::DeviceType::HUMIDITY: device = std::make_shared<HumiditySensor>(cfg); break;
            case IoTDevice::DeviceType::PRESSURE: device = std::make_shared<PressureSensor>(cfg); break;
        }
        devices.push_back(device);
        DatabaseManager::getInstance().saveDevice(cfg.device_id, device->getTypeString());
    }
    return devices;
}

}  // namespace

LiveRuntime::LiveRuntime() = default;

LiveRuntime::~LiveRuntime() {
    stop();
}

bool LiveRuntime::initialize(const SimulationConfig& config) {
    config_ = config;
    if (config_.device_count == 0U || config_.edge_node_count == 0U) {
        Logger::getInstance().error("LiveRuntime", "Device and edge-node counts must be greater than zero.");
        return false;
    }
    Logger::getInstance().info("LiveRuntime", "Initializing live runtime.");

    if (!DatabaseManager::getInstance().initialize(config_.db_path)) {
        return false;
    }
    if (!DatabaseManager::getInstance().resetSimulationData()) {
        return false;
    }

    TaskProfile::getInstance().loadProfiles(config_.db_path);
    for (const auto& entry : TaskProfile::getInstance().getAllProfiles()) {
        DatabaseManager::getInstance().saveTaskProfile(entry.first, entry.second);
    }

    monitor_ = std::make_shared<ResourceMonitor>();
    task_manager_ = std::make_shared<TaskManager>();
    bool valid_allocation = false;
    const auto allocation = allocationFromString(config_.strategy, valid_allocation);
    if (!valid_allocation) {
        return false;
    }
    bool valid_scheduling = false;
    const auto scheduling = schedulingFromString(config_.scheduling, valid_scheduling);
    if (!valid_scheduling) {
        return false;
    }

    allocator_ = std::make_shared<ResourceAllocator>(monitor_, allocation);
    scheduler_ = std::make_shared<TaskScheduler>(scheduling);
    executor_ = std::make_shared<TaskExecutor>(scheduler_);
    edges_ = makeEdges(config_.edge_node_count, monitor_);
    devices_ = makeDevices(config_.device_count);

    for (const auto& edge : edges_) {
        executor_->registerEdgeNode(edge);
    }

    generated_tasks_ = 0U;
    processing_complete_ = false;
    stop_requested_ = false;

    if (config_.automatic_mode) {
        chooseRecommendedConfiguration();
    }
    return true;
}

bool LiveRuntime::start() {
    if (running_.exchange(true)) {
        return true;
    }
    stop_requested_ = false;
    processing_complete_ = false;
    start_time_ = std::chrono::steady_clock::now();
    executor_->start();
    processing_thread_ = std::thread(&LiveRuntime::processingLoop, this);
    return true;
}

void LiveRuntime::stop() {
    stop_requested_ = true;
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
    if (executor_ != nullptr) {
        executor_->stop();
    }
    running_ = false;
}

void LiveRuntime::processingLoop() {
    auto last_snapshot = std::chrono::steady_clock::now();

    for (std::size_t i = 0U; i < config_.task_count && !stop_requested_; ++i) {
        if (config_.automatic_mode) {
            chooseRecommendedConfiguration();
        }

        const auto& device = devices_[i % devices_.size()];
        const json sensor_data = device->generateSensorData();
        const auto task = task_manager_->createTaskFromSensorData(
            device->getDeviceId(), device->getTypeString(), sensor_data);
        DatabaseManager::getInstance().saveTask(task);
        generated_tasks_ = i + 1U;

        ResourceAllocator::AllocationResult result;
        {
            std::lock_guard<std::mutex> lock(configuration_mutex_);
            result = allocator_->allocateTaskWithRetry(task, 200U, std::chrono::milliseconds(5));
            if (result.success) {
                const auto edge = monitor_->getEdgeNode(result.edge_id);
                scheduler_->scheduleTask(edge, task);
                DatabaseManager::getInstance().saveAllocation(
                    task->getTaskId(), result.edge_id, result.score, allocationToString(allocator_->getStrategy()));
            } else {
                DatabaseManager::getInstance().saveAllocationFailure(task->getTaskId(), result.reason);
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - last_snapshot >= std::chrono::milliseconds(250)) {
            recordResourceSnapshots();
            last_snapshot = now;
        }
    }

    // Drain queued work so the dashboard can show the final state without
    // abruptly terminating worker threads.
    const auto drain_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!stop_requested_ && std::chrono::steady_clock::now() < drain_deadline) {
        bool queued = false;
        for (const auto& edge : edges_) {
            if (edge->hasTasksInQueue()) {
                queued = true;
                break;
            }
        }
        if (!queued && executor_->getActiveTasks().empty()) {
            break;
        }
        recordResourceSnapshots();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    processing_complete_ = true;
    running_ = false;
    Logger::getInstance().info("LiveRuntime", "Live workload processing complete.");
}

void LiveRuntime::recordResourceSnapshots() {
    for (const auto& edge : edges_) {
        if (edge == nullptr) continue;
        const auto usage = edge->getCurrentUsage();
        DatabaseManager::getInstance().saveResourceSnapshot(
            edge->getEdgeId(), usage, edge->getQueueLength());
    }
}

void LiveRuntime::chooseRecommendedConfiguration() {
    if (allocator_ == nullptr || scheduler_ == nullptr || edges_.empty()) {
        return;
    }

    double cpu_total = 0.0;
    for (const auto& edge : edges_) {
        const auto capacity = edge->getCapacity();
        const auto usage = edge->getCurrentUsage();
        if (capacity.cpu_percent > 0.0) {
            cpu_total += (usage.cpu_percent / capacity.cpu_percent) * 100.0;
        }
    }
    const double average_cpu = cpu_total / static_cast<double>(edges_.size());

    // The recommendation is deliberately deterministic and based only on
    // live resource state; it does not replace the core algorithms.
    const auto allocation = (average_cpu >= 70.0)
        ? ResourceAllocator::AllocationStrategy::LEAST_LOAD
        : ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT;
    const auto scheduling = (average_cpu >= 60.0)
        ? TaskScheduler::SchedulingStrategy::EARLIEST_DEADLINE_FIRST
        : TaskScheduler::SchedulingStrategy::PRIORITY_BASED;

    std::lock_guard<std::mutex> lock(configuration_mutex_);
    allocator_->setStrategy(allocation);
    scheduler_->setStrategy(scheduling);
}

bool LiveRuntime::applyConfiguration(bool automatic,
                                     const std::string& allocation_strategy,
                                     const std::string& scheduling_algorithm,
                                     std::string& error) {
    if (allocator_ == nullptr || scheduler_ == nullptr) {
        error = "Runtime is not initialized";
        return false;
    }

    if (automatic) {
        {
            std::lock_guard<std::mutex> lock(configuration_mutex_);
            config_.automatic_mode = true;
        }
        chooseRecommendedConfiguration();
        return true;
    }

    std::lock_guard<std::mutex> lock(configuration_mutex_);

    bool valid_allocation = false;
    const auto allocation = allocationFromString(allocation_strategy, valid_allocation);
    if (!valid_allocation) {
        error = "Unsupported allocation strategy: " + allocation_strategy;
        return false;
    }
    bool valid_scheduling = false;
    const auto scheduling = schedulingFromString(scheduling_algorithm, valid_scheduling);
    if (!valid_scheduling) {
        error = "Unsupported scheduling algorithm: " + scheduling_algorithm;
        return false;
    }

    allocator_->setStrategy(allocation);
    scheduler_->setStrategy(scheduling);
    config_.automatic_mode = false;
    config_.strategy = allocationToString(allocation);
    config_.scheduling = schedulingToString(scheduling);
    return true;
}

json LiveRuntime::configuration() const {
    std::lock_guard<std::mutex> lock(configuration_mutex_);
    json item;
    item["automatic"] = config_.automatic_mode;
    item["requested_allocation_strategy"] = config_.automatic_mode ? "auto" : config_.strategy;
    item["requested_scheduling_algorithm"] = config_.automatic_mode ? "auto" : config_.scheduling;
    item["allocation_strategy"] = allocationToString(allocator_->getStrategy());
    item["allocation_strategy_label"] = allocationLabel(allocator_->getStrategy());
    item["scheduling_algorithm"] = schedulingToString(scheduler_->getStrategy());
    item["scheduling_algorithm_label"] = schedulingLabel(scheduler_->getStrategy());
    return item;
}

json LiveRuntime::status() const {
    json item;
    item["backend_available"] = true;
    item["running"] = running_.load();
    item["processing_complete"] = processing_complete_.load();
    item["devices"] = devices_.size();
    item["edge_nodes"] = edges_.size();
    item["tasks_configured"] = config_.task_count;
    item["tasks_generated"] = generated_tasks_.load();
    item["active_tasks"] = executor_ ? executor_->getActiveTasks().size() : 0U;
    item["available_edge_nodes"] = monitor_ ? monitor_->getAvailableNodeCount() : 0U;
    item["configuration"] = configuration();
    return item;
}

json LiveRuntime::devices() const {
    return DatabaseManager::getInstance().getDevicesJson();
}

json LiveRuntime::edgeNodes() const {
    json result = json::array();
    if (monitor_ == nullptr) return result;
    for (const auto& entry : monitor_->getAllEdgeNodes()) {
        const auto& edge = entry.second;
        if (edge == nullptr) continue;
        const auto capacity = edge->getCapacity();
        const auto usage = edge->getCurrentUsage();
        json item;
        item["edge_id"] = edge->getEdgeId();
        item["status"] = edge->isAvailable() ? "online" : "offline";
        item["cpu_capacity"] = capacity.cpu_percent;
        item["cpu_utilization"] = usage.cpu_percent;
        item["cpu_utilization_percent"] = capacity.cpu_percent > 0.0 ? (usage.cpu_percent / capacity.cpu_percent) * 100.0 : 0.0;
        item["ram_capacity_mb"] = capacity.ram_mb;
        item["ram_utilization_mb"] = usage.ram_mb;
        item["ram_utilization_percent"] = capacity.ram_mb > 0.0 ? (usage.ram_mb / capacity.ram_mb) * 100.0 : 0.0;
        item["bandwidth_capacity_mbps"] = capacity.bandwidth_mbps;
        item["bandwidth_utilization_mbps"] = usage.bandwidth_mbps;
        item["bandwidth_utilization_percent"] = capacity.bandwidth_mbps > 0.0 ? (usage.bandwidth_mbps / capacity.bandwidth_mbps) * 100.0 : 0.0;
        item["latency_ms"] = capacity.latency_ms;
        std::size_t executing_count = 0U;
        if (executor_ != nullptr) {
            for (const auto& active : executor_->getActiveTasks()) {
                if (active.value<std::string>("assigned_edge_id", "") == edge->getEdgeId()) {
                    ++executing_count;
                }
            }
        }
        item["active_tasks"] = executing_count;
        item["queue_length"] = edge->getQueueLength();
        item["load"] = item["cpu_utilization_percent"];
        result.push_back(item);
    }
    return result;
}

json LiveRuntime::tasks(std::size_t limit) const {
    return DatabaseManager::getInstance().getTasksJson(limit);
}

json LiveRuntime::activeTasks() const {
    json result = json::array();
    if (executor_ == nullptr) return result;
    for (const auto& item : executor_->getActiveTasks()) {
        result.push_back(item);
    }
    return result;
}

json LiveRuntime::allocations() const {
    return DatabaseManager::getInstance().getTaskAllocationHistory();
}

json LiveRuntime::metrics() const {
    json result;
    const double elapsed = std::max(
        0.001,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count());
    if (allocator_ != nullptr && executor_ != nullptr && monitor_ != nullptr) {
        MetricsCollector collector(allocator_, executor_, monitor_);
        result = collector.collectAsJson(elapsed);
    }
    result["tasks_generated"] = generated_tasks_.load();
    result["tasks_completed"] = executor_ ? executor_->getTotalTasksExecuted() : 0U;
    result["tasks_failed"] = executor_ ? executor_->getTotalTasksFailed() : 0U;
    return result;
}

json LiveRuntime::activities(std::size_t limit) const {
    return DatabaseManager::getInstance().getActivitiesJson(limit);
}

json LiveRuntime::alerts() const {
    return DatabaseManager::getInstance().getAlertsJson();
}

json LiveRuntime::resourceHistory(std::size_t limit) const {
    return DatabaseManager::getInstance().getResourceHistoryJson(limit);
}

json LiveRuntime::taskLifecycle(const std::string& task_id) const {
    json result;
    const auto task = DatabaseManager::getInstance().getTask(task_id);
    if (task == nullptr) {
        result["found"] = false;
        return result;
    }
    result["found"] = true;
    result["task"] = task->toJson();
    result["allocations"] = json::array();
    for (const auto& allocation : DatabaseManager::getInstance().getTaskAllocationHistory()) {
        if (allocation.value<std::string>("task_id", "") == task_id) {
            result["allocations"].push_back(allocation);
        }
    }
    const auto active = executor_ ? executor_->getActiveTasks() : std::vector<json>{};
    for (const auto& item : active) {
        if (item.value<std::string>("task_id", "") == task_id) {
            result["active"] = item;
        }
    }
    return result;
}

ResourceAllocator::AllocationStrategy LiveRuntime::allocationFromString(const std::string& value, bool& valid) {
    valid = true;
    if (value == "round_robin") return ResourceAllocator::AllocationStrategy::ROUND_ROBIN;
    if (value == "least_load") return ResourceAllocator::AllocationStrategy::LEAST_LOAD;
    if (value == "priority_based") return ResourceAllocator::AllocationStrategy::PRIORITY_BASED;
    if (value == "multi_constraint") return ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT;
    valid = false;
    return ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT;
}

TaskScheduler::SchedulingStrategy LiveRuntime::schedulingFromString(const std::string& value, bool& valid) {
    valid = true;
    if (value == "fifo") return TaskScheduler::SchedulingStrategy::FIFO;
    if (value == "priority_based") return TaskScheduler::SchedulingStrategy::PRIORITY_BASED;
    if (value == "edf") return TaskScheduler::SchedulingStrategy::EARLIEST_DEADLINE_FIRST;
    if (value == "round_robin") return TaskScheduler::SchedulingStrategy::ROUND_ROBIN_TIME_SLICE;
    valid = false;
    return TaskScheduler::SchedulingStrategy::PRIORITY_BASED;
}

std::string LiveRuntime::allocationToString(ResourceAllocator::AllocationStrategy strategy) {
    switch (strategy) {
        case ResourceAllocator::AllocationStrategy::ROUND_ROBIN: return "round_robin";
        case ResourceAllocator::AllocationStrategy::LEAST_LOAD: return "least_load";
        case ResourceAllocator::AllocationStrategy::PRIORITY_BASED: return "priority_based";
        case ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT: return "multi_constraint";
    }
    return "multi_constraint";
}

std::string LiveRuntime::allocationLabel(ResourceAllocator::AllocationStrategy strategy) {
    switch (strategy) {
        case ResourceAllocator::AllocationStrategy::ROUND_ROBIN: return "Round Robin";
        case ResourceAllocator::AllocationStrategy::LEAST_LOAD: return "Least Load";
        case ResourceAllocator::AllocationStrategy::PRIORITY_BASED: return "Priority Based";
        case ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT: return "Multi-Constraint";
    }
    return "Multi-Constraint";
}

std::string LiveRuntime::schedulingToString(TaskScheduler::SchedulingStrategy strategy) {
    switch (strategy) {
        case TaskScheduler::SchedulingStrategy::FIFO: return "fifo";
        case TaskScheduler::SchedulingStrategy::PRIORITY_BASED: return "priority_based";
        case TaskScheduler::SchedulingStrategy::EARLIEST_DEADLINE_FIRST: return "edf";
        case TaskScheduler::SchedulingStrategy::ROUND_ROBIN_TIME_SLICE: return "round_robin";
    }
    return "priority_based";
}

std::string LiveRuntime::schedulingLabel(TaskScheduler::SchedulingStrategy strategy) {
    switch (strategy) {
        case TaskScheduler::SchedulingStrategy::FIFO: return "FIFO";
        case TaskScheduler::SchedulingStrategy::PRIORITY_BASED: return "Priority Based";
        case TaskScheduler::SchedulingStrategy::EARLIEST_DEADLINE_FIRST: return "EDF (Earliest Deadline First)";
        case TaskScheduler::SchedulingStrategy::ROUND_ROBIN_TIME_SLICE: return "Round Robin";
    }
    return "Priority Based";
}
