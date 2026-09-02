#include "simulation_config.h"
#include "common/logger.h"
#include "common/constants.h"

#include "iot_device/iot_device.h"
#include "iot_device/sensor_simulator.h"
#include "task/task.h"
#include "task/task_manager.h"
#include "task/task_profile.h"
#include "edge/edge_node.h"
#include "edge/resource_monitor.h"
#include "resource/resource_allocator.h"
#include "resource/resource_controller.h"
#include "scheduler/task_scheduler.h"
#include "executor/task_executor.h"
#include "database/database_manager.h"
#include "metrics/metrics_collector.h"
#include "api/live_runtime.h"
#include "api/http_api_server.h"

#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <random>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace {

ResourceAllocator::AllocationStrategy strategyFromString(const std::string& name) {
    if (name == "round_robin") {
        return ResourceAllocator::AllocationStrategy::ROUND_ROBIN;
    }
    if (name == "least_load") {
        return ResourceAllocator::AllocationStrategy::LEAST_LOAD;
    }
    if (name == "priority_based") {
        return ResourceAllocator::AllocationStrategy::PRIORITY_BASED;
    }
    return ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT;
}

std::string strategyToLabel(ResourceAllocator::AllocationStrategy strategy) {
    switch (strategy) {
        case ResourceAllocator::AllocationStrategy::ROUND_ROBIN:
            return "Round Robin";
        case ResourceAllocator::AllocationStrategy::LEAST_LOAD:
            return "Least Load";
        case ResourceAllocator::AllocationStrategy::PRIORITY_BASED:
            return "Priority Based";
        case ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT:
            return "Multi-Constraint";
        default:
            return "Unknown";
    }
}

TaskScheduler::SchedulingStrategy schedulingFromString(const std::string& name) {
    if (name == "fifo") return TaskScheduler::SchedulingStrategy::FIFO;
    if (name == "edf") return TaskScheduler::SchedulingStrategy::EARLIEST_DEADLINE_FIRST;
    if (name == "round_robin") return TaskScheduler::SchedulingStrategy::ROUND_ROBIN_TIME_SLICE;
    return TaskScheduler::SchedulingStrategy::PRIORITY_BASED;
}

std::string schedulingToLabel(TaskScheduler::SchedulingStrategy strategy) {
    switch (strategy) {
        case TaskScheduler::SchedulingStrategy::FIFO:
            return "FIFO";
        case TaskScheduler::SchedulingStrategy::PRIORITY_BASED:
            return "Priority Based";
        case TaskScheduler::SchedulingStrategy::EARLIEST_DEADLINE_FIRST:
            return "Earliest Deadline First (EDF)";
        case TaskScheduler::SchedulingStrategy::ROUND_ROBIN_TIME_SLICE:
            return "Round Robin Time Slice";
        default:
            return "Unknown";
    }
}

/**
 * Builds the edge-node fleet described in spec §7, scaled to
 * config.edge_node_count. The first three nodes match the spec's
 * worked examples (E01/E02/E03) exactly; additional nodes get
 * randomized-but-plausible capacities.
 */
std::vector<std::shared_ptr<EdgeNode>> createEdgeNodes(
    std::size_t count, std::shared_ptr<ResourceMonitor> monitor) {

    std::vector<std::shared_ptr<EdgeNode>> nodes;
    nodes.reserve(count);

    static thread_local std::mt19937 generator(42);  // Fixed seed: reproducible experiment comparisons.
    std::uniform_real_distribution<double> ram_dist(4096.0, 32768.0);
    std::uniform_real_distribution<double> bandwidth_dist(50.0, 200.0);
    std::uniform_int_distribution<int64_t> latency_dist(8, 40);

    for (std::size_t i = 0U; i < count; ++i) {
        std::ostringstream id;
        id << "E" << std::setfill('0') << std::setw(2) << (i + 1U);

        EdgeNode::ResourceCapacity capacity;
        capacity.cpu_percent = 100.0;
        capacity.ram_mb = ram_dist(generator);
        capacity.bandwidth_mbps = bandwidth_dist(generator);
        capacity.latency_ms = latency_dist(generator);

        auto node = std::make_shared<EdgeNode>(id.str(), capacity);
        nodes.push_back(node);
        monitor->registerEdgeNode(node);

        DatabaseManager::getInstance().saveEdgeNode(node);
    }

    return nodes;
}

/**
 * Builds the IoT device fleet (spec §16: "100 simulated IoT devices"),
 * distributed across the sensor types this implementation supports.
 */
std::vector<std::shared_ptr<IoTDevice>> createDevices(std::size_t count) {
    std::vector<std::shared_ptr<IoTDevice>> devices;
    devices.reserve(count);

    static const std::vector<IoTDevice::DeviceType> kTypes = {
        IoTDevice::DeviceType::ULTRASONIC,
        IoTDevice::DeviceType::TEMPERATURE,
        IoTDevice::DeviceType::CAMERA,
        IoTDevice::DeviceType::HUMIDITY,
        IoTDevice::DeviceType::PRESSURE
    };

    for (std::size_t i = 0U; i < count; ++i) {
        const IoTDevice::DeviceType type = kTypes[i % kTypes.size()];

        std::ostringstream id;
        id << "DEV" << std::setfill('0') << std::setw(4) << (i + 1U);

        IoTDevice::DeviceConfig config;
        config.device_id = id.str();
        config.type = type;
        config.data_generation_interval_ms = 200.0;
        config.data_variance = 0.05;

        std::shared_ptr<IoTDevice> device;
        switch (type) {
            case IoTDevice::DeviceType::ULTRASONIC:
                device = std::make_shared<UltrasonicSensor>(config);
                break;
            case IoTDevice::DeviceType::TEMPERATURE:
                device = std::make_shared<TemperatureSensor>(config);
                break;
            case IoTDevice::DeviceType::CAMERA:
                device = std::make_shared<CameraSensor>(config);
                break;
            case IoTDevice::DeviceType::HUMIDITY:
                device = std::make_shared<HumiditySensor>(config);
                break;
            case IoTDevice::DeviceType::PRESSURE:
                device = std::make_shared<PressureSensor>(config);
                break;
            default:
                device = std::make_shared<UltrasonicSensor>(config);
                break;
        }

        devices.push_back(device);
        DatabaseManager::getInstance().saveDevice(config.device_id, device->getTypeString());
    }

    return devices;
}

/**
 * Runs one full experiment: generates `task_count` tasks from the
 * device fleet using the given allocation strategy, allocates them,
 * executes them, and returns a metrics snapshot. This is the unit of
 * work spec §18 calls "Experiment N".
 */
MetricsCollector::Snapshot runExperiment(
    const SimulationConfig& config,
    ResourceAllocator::AllocationStrategy strategy,
    const std::vector<std::shared_ptr<IoTDevice>>& devices) {

    Logger& logger = Logger::getInstance();
    logger.info("Simulation", "=== Running experiment: " + strategyToLabel(strategy) + " ===");

    auto monitor = std::make_shared<ResourceMonitor>();
    const std::vector<std::shared_ptr<EdgeNode>> edges = createEdgeNodes(config.edge_node_count, monitor);

    auto task_manager = std::make_shared<TaskManager>();
    auto allocator = std::make_shared<ResourceAllocator>(monitor, strategy);
    auto scheduler = std::make_shared<TaskScheduler>(schedulingFromString(config.scheduling));
    auto executor = std::make_shared<TaskExecutor>(scheduler);

    for (const auto& edge : edges) {
        executor->registerEdgeNode(edge);
    }

    // Start workers before producing tasks. This provides real execution
    // concurrency, so resources are released as tasks complete instead of
    // accumulating reservations for all 10,000 generated tasks.
    executor->start();

    const auto start_time = std::chrono::steady_clock::now();

    // Generate and allocate tasks. Devices cycle round-robin to reach
    // config.task_count total tasks regardless of device_count.
    for (std::size_t i = 0U; i < config.task_count; ++i) {
        const std::shared_ptr<IoTDevice>& device = devices[i % devices.size()];
        const json sensor_data = device->generateSensorData();

        std::shared_ptr<Task> task = task_manager->createTaskFromSensorData(
            device->getDeviceId(), device->getTypeString(), sensor_data);

        logger.debug("TaskGeneration",
            "Generated " + task->getTaskId() + " from device " + device->getDeviceId() +
            " (sensor=" + device->getTypeString() + ")");

        DatabaseManager::getInstance().saveTask(task);

        // Apply bounded back-pressure. A busy system is not treated as a
        // permanent allocation failure; the request waits for a node to
        // release resources after executing an earlier task.
        const ResourceAllocator::AllocationResult result =
            allocator->allocateTaskWithRetry(task, 200U, std::chrono::milliseconds(5));

        if (result.success) {
            // ResourceAllocator reserves resources; TaskScheduler owns the
            // execution queue. Enqueue exactly once here.
            scheduler->scheduleTask(monitor->getEdgeNode(result.edge_id), task);
            DatabaseManager::getInstance().saveAllocation(
                task->getTaskId(), result.edge_id, result.score, config.strategy);
        } else {
            DatabaseManager::getInstance().saveAllocationFailure(
                task->getTaskId(), result.reason);
        }

        // Periodic background resource fluctuation, matching spec §7/§9
        // ("the simulator continuously changes their resource usage").
        if ((i % 100U) == 0U) {
            monitor->updateResourceMetrics();
        }
    }

    // Let the executor drain the queues. In a real interactive run this
    // would be event-driven; here we poll until queues empty or a
    // generous timeout elapses, to keep experiment runs bounded.
    constexpr int kMaxDrainSeconds = 30;
    const auto drain_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kMaxDrainSeconds);

    bool any_queued = true;
    while (any_queued && std::chrono::steady_clock::now() < drain_deadline) {
        any_queued = false;
        for (const auto& edge : edges) {
            if (edge->hasTasksInQueue()) {
                any_queued = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    executor->stop();

    const auto end_time = std::chrono::steady_clock::now();
    const double elapsed_seconds =
        std::chrono::duration<double>(end_time - start_time).count();

    MetricsCollector collector(allocator, executor, monitor);
    const MetricsCollector::Snapshot snapshot = collector.collect(elapsed_seconds);

    logger.info("Simulation",
        "Experiment complete: " + std::to_string(allocator->getTotalAllocations()) +
        " tasks processed, " + std::to_string(allocator->getSuccessRate()) + "% allocated successfully.");

    return snapshot;
}

}  // namespace

int main(int argc, char** argv) {
    const SimulationConfig config = SimulationConfig::parseArgs(argc, argv);

    Logger& logger = Logger::getInstance();
    logger.setLogFile(config.log_path);
    if (config.verbose) {
        logger.setMinLevel(Logger::Level::DEBUG);
    }
    logger.info("Simulation", "Edge IoT Simulator starting.");
    logger.info("Simulation",
        "Config: devices=" + std::to_string(config.device_count) +
        " edges=" + std::to_string(config.edge_node_count) +
        " tasks=" + std::to_string(config.task_count));
    logger.info("Simulation",
        "Allocation strategy: " + strategyToLabel(strategyFromString(config.strategy)));
    logger.info("Simulation",
        "Scheduling algorithm: " + schedulingToLabel(schedulingFromString(config.scheduling)));
    if (config.verbose) {
        logger.info("Simulation", "Verbose trace: ENABLED");
    }

    if (config.web_mode) {
        auto runtime = std::make_shared<LiveRuntime>();
        if (!runtime->initialize(config)) {
            logger.error("LiveRuntime", "Failed to initialize live runtime.");
            DatabaseManager::getInstance().close();
            return 1;
        }
        HttpApiServer server(runtime);
        if (!server.start(config.web_port)) {
            logger.error("HttpApi", "Failed to bind HTTP API on port " + std::to_string(config.web_port));
            runtime->stop();
            DatabaseManager::getInstance().close();
            return 1;
        }
        if (!runtime->start()) {
            logger.error("LiveRuntime", "Failed to start live processing.");
            server.stop();
            DatabaseManager::getInstance().close();
            return 1;
        }

        logger.info("HttpApi", "Dashboard API listening on http://localhost:" + std::to_string(config.web_port));
        while (server.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (runtime->isProcessingComplete()) {
                // Keep the API available after the workload completes so the dashboard
                // remains inspectable. Ctrl+C terminates the process normally.
            }
        }
        runtime->stop();
        DatabaseManager::getInstance().close();
        return 0;
    }

    if (!DatabaseManager::getInstance().initialize(config.db_path)) {
        logger.error("Simulation", "Failed to initialize database at " + config.db_path);
        return 1;
    }

    if (!DatabaseManager::getInstance().resetSimulationData()) {
        logger.error("Simulation", "Failed to reset previous simulation data");
        DatabaseManager::getInstance().close();
        return 1;
    }

    TaskProfile::getInstance().loadProfiles(config.db_path);
    for (const auto& profile_entry : TaskProfile::getInstance().getAllProfiles()) {
        DatabaseManager::getInstance().saveTaskProfile(profile_entry.first, profile_entry.second);
    }

    const std::vector<std::shared_ptr<IoTDevice>> devices = createDevices(config.device_count);

    if (config.run_comparison) {
        // Spec §18: run Round Robin, Least Load, Priority Based, and
        // Multi-Constraint back-to-back on the same workload shape and
        // print a side-by-side comparison table.
        static const std::vector<ResourceAllocator::AllocationStrategy> kAllStrategies = {
            ResourceAllocator::AllocationStrategy::ROUND_ROBIN,
            ResourceAllocator::AllocationStrategy::LEAST_LOAD,
            ResourceAllocator::AllocationStrategy::PRIORITY_BASED,
            ResourceAllocator::AllocationStrategy::MULTI_CONSTRAINT
        };

        std::vector<std::pair<std::string, MetricsCollector::Snapshot>> results;
        for (const auto strategy : kAllStrategies) {
            const MetricsCollector::Snapshot snapshot = runExperiment(config, strategy, devices);
            results.emplace_back(strategyToLabel(strategy), snapshot);
        }

        std::cout << "\n=== Strategy Comparison (spec section 18) ===\n\n";
        std::cout << MetricsCollector::formatComparisonTable(results);
    } else {
        const ResourceAllocator::AllocationStrategy strategy = strategyFromString(config.strategy);
        const MetricsCollector::Snapshot snapshot = runExperiment(config, strategy, devices);

        std::cout << "\n=== Simulation Results (" << strategyToLabel(strategy) << ") ===\n";
        std::cout << "Average waiting time:      " << snapshot.average_waiting_time_ms << " ms\n";
        std::cout << "Average latency:           " << snapshot.average_latency_ms << " ms\n";
        std::cout << "Deadline miss rate:        " << snapshot.deadline_miss_rate_percent << " %\n";
        std::cout << "Average CPU utilization:   " << snapshot.average_cpu_utilization_percent << " %\n";
        std::cout << "Load imbalance (stddev):   " << snapshot.load_imbalance_stddev << "\n";
        std::cout << "Throughput:                " << snapshot.throughput_tasks_per_second << " tasks/sec\n";
        std::cout << "Task success rate:         " << snapshot.task_success_rate_percent << " %\n";
    }

    DatabaseManager::getInstance().close();
    logger.info("Simulation", "Edge IoT Simulator finished.");
    return 0;
}
