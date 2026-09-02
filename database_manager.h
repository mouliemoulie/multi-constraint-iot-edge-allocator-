#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include "../task/task.h"
#include "../edge/edge_node.h"
#include <sqlite3.h>
#include <string>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * Database Manager handles all SQLite persistence
 * Stores tasks, allocations, execution results, and metrics
 */
class DatabaseManager {
public:
    static DatabaseManager& getInstance();

    // Initialize database
    bool initialize(const std::string& db_path);
    void close();
    bool isInitialized() const { return db_initialized_; }

    // Schema creation
    bool createSchemas();
    bool resetSimulationData();

    // Task operations
    bool saveTask(const std::shared_ptr<Task>& task);
    bool updateTaskStatus(const std::string& task_id, Task::TaskStatus status);
    std::shared_ptr<Task> getTask(const std::string& task_id);
    std::vector<std::shared_ptr<Task>> getAllTasks();

    // Allocation operations
    bool saveAllocation(
        const std::string& task_id,
        const std::string& edge_id,
        double score,
        const std::string& strategy = ""
    );
    bool saveAllocationFailure(
        const std::string& task_id,
        const std::string& reason
    );

    // Edge node operations
    bool saveEdgeNode(const std::shared_ptr<EdgeNode>& node);
    bool updateEdgeNodeResources(
        const std::string& edge_id,
        const EdgeNode::ResourceUsage& usage
    );

    // Execution results
    bool saveTaskExecution(
        const std::string& task_id,
        int64_t execution_time_ms,
        bool success
    );

    // Resource history
    bool saveResourceSnapshot(
        const std::string& edge_id,
        const EdgeNode::ResourceUsage& usage,
        int64_t queue_length
    );

    // Device operations
    bool saveDevice(
        const std::string& device_id,
        const std::string& device_type
    );

    // Task profiles
    bool saveTaskProfile(
        const std::string& sensor_type,
        const Task::TaskRequirements& requirements
    );

    // Query operations for analytics
    std::vector<json> getTaskAllocationHistory();
    std::vector<json> getEdgeResourceHistory(const std::string& edge_id);
    json getSystemMetrics();
    std::vector<json> getDevicesJson();
    std::vector<json> getEdgeNodesJson();
    std::vector<json> getTasksJson(std::size_t limit = 500);
    std::vector<json> getActiveTasksJson();
    std::vector<json> getActivitiesJson(std::size_t limit = 50);
    std::vector<json> getAlertsJson();
    std::vector<json> getResourceHistoryJson(std::size_t limit = 120);

private:
    DatabaseManager() = default;
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    sqlite3* db_;
    bool db_initialized_;

    // Helper methods
    bool executeSql(const std::string& sql);
    sqlite3_stmt* prepareStatement(const std::string& sql);
};

#endif // DATABASE_MANAGER_H
