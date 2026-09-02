#include "database/database_manager.h"
#include "common/logger.h"

#include <sstream>

DatabaseManager& DatabaseManager::getInstance() {
    static DatabaseManager instance;
    return instance;
}

bool DatabaseManager::initialize(const std::string& db_path) {
    const int result = sqlite3_open(db_path.c_str(), &db_);
    if (result != SQLITE_OK) {
        Logger::getInstance().error("DatabaseManager",
            "Failed to open database: " + std::string(sqlite3_errmsg(db_)));
        db_initialized_ = false;
        return false;
    }

    // Enable foreign keys and WAL mode for better concurrent read/write
    // behavior under the multithreaded simulation.
    executeSql("PRAGMA foreign_keys = ON;");
    executeSql("PRAGMA journal_mode = WAL;");

    db_initialized_ = createSchemas();
    return db_initialized_;
}

void DatabaseManager::close() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    db_initialized_ = false;
}

bool DatabaseManager::executeSql(const std::string& sql) {
    char* error_message = nullptr;
    const int result = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_message);

    if (result != SQLITE_OK) {
        const std::string message = (error_message != nullptr) ? error_message : "unknown error";
        Logger::getInstance().error("DatabaseManager", "SQL error: " + message);
        if (error_message != nullptr) {
            sqlite3_free(error_message);
        }
        return false;
    }
    return true;
}

sqlite3_stmt* DatabaseManager::prepareStatement(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    const int result = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        Logger::getInstance().error("DatabaseManager",
            "Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
        return nullptr;
    }
    return stmt;
}

bool DatabaseManager::createSchemas() {
    // Spec §12: devices, task_profiles, tasks, edge_nodes,
    // task_allocations, task_execution, resource_history.
    static const char* const kSchemas[] = {
        "CREATE TABLE IF NOT EXISTS devices ("
        "  device_id TEXT PRIMARY KEY,"
        "  device_type TEXT NOT NULL"
        ");",

        "CREATE TABLE IF NOT EXISTS task_profiles ("
        "  sensor_type TEXT PRIMARY KEY,"
        "  cpu_percent REAL NOT NULL,"
        "  ram_mb REAL NOT NULL,"
        "  bandwidth_mbps REAL NOT NULL,"
        "  deadline_ms INTEGER NOT NULL,"
        "  priority INTEGER NOT NULL"
        ");",

        "CREATE TABLE IF NOT EXISTS edge_nodes ("
        "  edge_id TEXT PRIMARY KEY,"
        "  cpu_capacity REAL NOT NULL,"
        "  ram_capacity_mb REAL NOT NULL,"
        "  bandwidth_capacity_mbps REAL NOT NULL,"
        "  latency_ms INTEGER NOT NULL"
        ");",

        "CREATE TABLE IF NOT EXISTS tasks ("
        "  task_id TEXT PRIMARY KEY,"
        "  device_id TEXT NOT NULL,"
        "  sensor_type TEXT NOT NULL,"
        "  cpu_percent REAL NOT NULL,"
        "  ram_mb REAL NOT NULL,"
        "  bandwidth_mbps REAL NOT NULL,"
        "  deadline_ms INTEGER NOT NULL,"
        "  priority INTEGER NOT NULL,"
        "  status INTEGER NOT NULL,"
        "  created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY (device_id) REFERENCES devices(device_id)"
        ");",

        "CREATE TABLE IF NOT EXISTS task_allocations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  task_id TEXT NOT NULL,"
        "  edge_id TEXT,"
        "  score REAL,"
        "  success INTEGER NOT NULL,"
        "  strategy TEXT,"
        "  reason TEXT,"
        "  allocated_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY (task_id) REFERENCES tasks(task_id)"
        ");",

        "CREATE TABLE IF NOT EXISTS task_execution ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  task_id TEXT NOT NULL,"
        "  execution_time_ms INTEGER NOT NULL,"
        "  success INTEGER NOT NULL,"
        "  executed_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY (task_id) REFERENCES tasks(task_id)"
        ");",

        "CREATE TABLE IF NOT EXISTS resource_history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  edge_id TEXT NOT NULL,"
        "  cpu_percent REAL NOT NULL,"
        "  ram_mb REAL NOT NULL,"
        "  bandwidth_mbps REAL NOT NULL,"
        "  queue_length INTEGER NOT NULL,"
        "  recorded_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY (edge_id) REFERENCES edge_nodes(edge_id)"
        ");",

        "CREATE INDEX IF NOT EXISTS idx_tasks_device ON tasks(device_id);",
        "CREATE INDEX IF NOT EXISTS idx_allocations_task ON task_allocations(task_id);",
        "CREATE INDEX IF NOT EXISTS idx_execution_task ON task_execution(task_id);",
        "CREATE INDEX IF NOT EXISTS idx_resource_history_edge ON resource_history(edge_id);"
    };

    for (const char* schema : kSchemas) {
        if (!executeSql(schema)) {
            return false;
        }
    }

    // Backward-compatible migration for databases created by older builds.
    // The frontend needs to know which allocation strategy produced a row.
    char* migration_error = nullptr;
    const int migration_result = sqlite3_exec(
        db_, "ALTER TABLE task_allocations ADD COLUMN strategy TEXT;", nullptr, nullptr, &migration_error);
    if (migration_result != SQLITE_OK) {
        const std::string message = (migration_error != nullptr) ? migration_error : "unknown error";
        if (message.find("duplicate column name") == std::string::npos) {
            Logger::getInstance().error("DatabaseManager", "Schema migration error: " + message);
            if (migration_error != nullptr) {
                sqlite3_free(migration_error);
            }
            return false;
        }
        if (migration_error != nullptr) {
            sqlite3_free(migration_error);
        }
    }
    return true;
}

bool DatabaseManager::resetSimulationData() {
    // Each simulator invocation is a fresh experiment. Keeping old
    // allocation/execution rows makes task IDs and success rates from a
    // previous run contaminate the current experiment, especially when
    // --compare runs four experiments with the same task IDs.
    static const char* const kResetSql =
        "DELETE FROM task_execution;"
        "DELETE FROM task_allocations;"
        "DELETE FROM resource_history;"
        "DELETE FROM tasks;"
        "DELETE FROM edge_nodes;"
        "DELETE FROM devices;";
    return executeSql(kResetSql);
}

bool DatabaseManager::saveDevice(const std::string& device_id, const std::string& device_type) {
    sqlite3_stmt* stmt = prepareStatement(
        "INSERT OR REPLACE INTO devices (device_id, device_type) VALUES (?, ?);");
    if (stmt == nullptr) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, device_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, device_type.c_str(), -1, SQLITE_TRANSIENT);

    const bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool DatabaseManager::saveTaskProfile(const std::string& sensor_type, const Task::TaskRequirements& requirements) {
    sqlite3_stmt* stmt = prepareStatement(
        "INSERT OR REPLACE INTO task_profiles "
        "(sensor_type, cpu_percent, ram_mb, bandwidth_mbps, deadline_ms, priority) "
        "VALUES (?, ?, ?, ?, ?, ?);");
    if (stmt == nullptr) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, sensor_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, requirements.cpu_percent);
    sqlite3_bind_double(stmt, 3, requirements.ram_mb);
    sqlite3_bind_double(stmt, 4, requirements.bandwidth_mbps);
    sqlite3_bind_int64(stmt, 5, requirements.deadline_ms);
    sqlite3_bind_int(stmt, 6, static_cast<int>(requirements.priority));

    const bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool DatabaseManager::saveEdgeNode(const std::shared_ptr<EdgeNode>& node) {
    if (node == nullptr) {
        return false;
    }

    sqlite3_stmt* stmt = prepareStatement(
        "INSERT OR REPLACE INTO edge_nodes "
        "(edge_id, cpu_capacity, ram_capacity_mb, bandwidth_capacity_mbps, latency_ms) "
        "VALUES (?, ?, ?, ?, ?);");
    if (stmt == nullptr) {
        return false;
    }

    const EdgeNode::ResourceCapacity capacity = node->getCapacity();

    sqlite3_bind_text(stmt, 1, node->getEdgeId().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, capacity.cpu_percent);
    sqlite3_bind_double(stmt, 3, capacity.ram_mb);
    sqlite3_bind_double(stmt, 4, capacity.bandwidth_mbps);
    sqlite3_bind_int64(stmt, 5, capacity.latency_ms);

    const bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool DatabaseManager::saveTask(const std::shared_ptr<Task>& task) {
    if (task == nullptr) {
        return false;
    }

    sqlite3_stmt* stmt = prepareStatement(
        "INSERT OR REPLACE INTO tasks "
        "(task_id, device_id, sensor_type, cpu_percent, ram_mb, bandwidth_mbps, "
        " deadline_ms, priority, status) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");
    if (stmt == nullptr) {
        return false;
    }

    const Task::TaskRequirements requirements = task->getRequirements();

    sqlite3_bind_text(stmt, 1, task->getTaskId().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, task->getDeviceId().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, task->getSensorType().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, requirements.cpu_percent);
    sqlite3_bind_double(stmt, 5, requirements.ram_mb);
    sqlite3_bind_double(stmt, 6, requirements.bandwidth_mbps);
    sqlite3_bind_int64(stmt, 7, requirements.deadline_ms);
    sqlite3_bind_int(stmt, 8, static_cast<int>(requirements.priority));
    sqlite3_bind_int(stmt, 9, static_cast<int>(task->getStatus()));

    const bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool DatabaseManager::updateTaskStatus(const std::string& task_id, Task::TaskStatus status) {
    sqlite3_stmt* stmt = prepareStatement("UPDATE tasks SET status = ? WHERE task_id = ?;");
    if (stmt == nullptr) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(status));
    sqlite3_bind_text(stmt, 2, task_id.c_str(), -1, SQLITE_TRANSIENT);

    const bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool DatabaseManager::saveAllocation(const std::string& task_id, const std::string& edge_id, double score, const std::string& strategy) {
    sqlite3_stmt* stmt = prepareStatement(
        "INSERT INTO task_allocations (task_id, edge_id, score, success, reason, strategy) "
        "VALUES (?, ?, ?, 1, 'allocated', ?);");
    if (stmt == nullptr) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, edge_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, score);
    sqlite3_bind_text(stmt, 4, strategy.c_str(), -1, SQLITE_TRANSIENT);

    const bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool DatabaseManager::saveAllocationFailure(const std::string& task_id, const std::string& reason) {
    sqlite3_stmt* stmt = prepareStatement(
        "INSERT INTO task_allocations (task_id, edge_id, score, success, reason) "
        "VALUES (?, NULL, NULL, 0, ?);");
    if (stmt == nullptr) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, reason.c_str(), -1, SQLITE_TRANSIENT);

    const bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool DatabaseManager::saveTaskExecution(const std::string& task_id, int64_t execution_time_ms, bool success_flag) {
    sqlite3_stmt* stmt = prepareStatement(
        "INSERT INTO task_execution (task_id, execution_time_ms, success) VALUES (?, ?, ?);");
    if (stmt == nullptr) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, execution_time_ms);
    sqlite3_bind_int(stmt, 3, success_flag ? 1 : 0);

    const bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool DatabaseManager::saveResourceSnapshot(
    const std::string& edge_id,
    const EdgeNode::ResourceUsage& usage,
    int64_t queue_length) {

    sqlite3_stmt* stmt = prepareStatement(
        "INSERT INTO resource_history "
        "(edge_id, cpu_percent, ram_mb, bandwidth_mbps, queue_length) "
        "VALUES (?, ?, ?, ?, ?);");
    if (stmt == nullptr) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, edge_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, usage.cpu_percent);
    sqlite3_bind_double(stmt, 3, usage.ram_mb);
    sqlite3_bind_double(stmt, 4, usage.bandwidth_mbps);
    sqlite3_bind_int64(stmt, 5, queue_length);

    const bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

std::shared_ptr<Task> DatabaseManager::getTask(const std::string& task_id) {
    sqlite3_stmt* stmt = prepareStatement(
        "SELECT task_id, device_id, sensor_type, cpu_percent, ram_mb, bandwidth_mbps, "
        "deadline_ms, priority, status FROM tasks WHERE task_id = ?;");
    if (stmt == nullptr) {
        return nullptr;
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);

    std::shared_ptr<Task> result = nullptr;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Task::TaskRequirements requirements;
        requirements.cpu_percent = sqlite3_column_double(stmt, 3);
        requirements.ram_mb = sqlite3_column_double(stmt, 4);
        requirements.bandwidth_mbps = sqlite3_column_double(stmt, 5);
        requirements.deadline_ms = sqlite3_column_int64(stmt, 6);
        requirements.priority = static_cast<Task::TaskPriority>(sqlite3_column_int(stmt, 7));

        const std::string device_id =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const std::string sensor_type =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

        result = std::make_shared<Task>(task_id, device_id, sensor_type, requirements, json::object());
        result->setStatus(static_cast<Task::TaskStatus>(sqlite3_column_int(stmt, 8)));
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::shared_ptr<Task>> DatabaseManager::getAllTasks() {
    std::vector<std::shared_ptr<Task>> results;

    sqlite3_stmt* stmt = prepareStatement(
        "SELECT task_id, device_id, sensor_type, cpu_percent, ram_mb, bandwidth_mbps, "
        "deadline_ms, priority, status FROM tasks;");
    if (stmt == nullptr) {
        return results;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Task::TaskRequirements requirements;
        requirements.cpu_percent = sqlite3_column_double(stmt, 3);
        requirements.ram_mb = sqlite3_column_double(stmt, 4);
        requirements.bandwidth_mbps = sqlite3_column_double(stmt, 5);
        requirements.deadline_ms = sqlite3_column_int64(stmt, 6);
        requirements.priority = static_cast<Task::TaskPriority>(sqlite3_column_int(stmt, 7));

        const std::string task_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const std::string device_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const std::string sensor_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

        auto task = std::make_shared<Task>(task_id, device_id, sensor_type, requirements, json::object());
        task->setStatus(static_cast<Task::TaskStatus>(sqlite3_column_int(stmt, 8)));
        results.push_back(task);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<json> DatabaseManager::getTaskAllocationHistory() {
    std::vector<json> results;

    sqlite3_stmt* stmt = prepareStatement(
        "SELECT task_id, edge_id, score, success, reason, allocated_at, strategy "
        "FROM task_allocations ORDER BY id DESC LIMIT 1000;");
    if (stmt == nullptr) {
        return results;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json entry;
        entry["task_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

        const unsigned char* edge_text = sqlite3_column_text(stmt, 1);
        entry["edge_id"] = (edge_text != nullptr) ? reinterpret_cast<const char*>(edge_text) : "";

        entry["score"] = sqlite3_column_double(stmt, 2);
        entry["success"] = (sqlite3_column_int(stmt, 3) != 0);

        const unsigned char* reason_text = sqlite3_column_text(stmt, 4);
        entry["reason"] = (reason_text != nullptr) ? reinterpret_cast<const char*>(reason_text) : "";

        entry["allocated_at"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        const unsigned char* strategy_text = sqlite3_column_text(stmt, 6);
        entry["strategy"] = (strategy_text != nullptr) ? reinterpret_cast<const char*>(strategy_text) : "";

        results.push_back(entry);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<json> DatabaseManager::getEdgeResourceHistory(const std::string& edge_id) {
    std::vector<json> results;

    sqlite3_stmt* stmt = prepareStatement(
        "SELECT cpu_percent, ram_mb, bandwidth_mbps, queue_length, recorded_at "
        "FROM resource_history WHERE edge_id = ? ORDER BY id DESC LIMIT 1000;");
    if (stmt == nullptr) {
        return results;
    }

    sqlite3_bind_text(stmt, 1, edge_id.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json entry;
        entry["cpu_percent"] = sqlite3_column_double(stmt, 0);
        entry["ram_mb"] = sqlite3_column_double(stmt, 1);
        entry["bandwidth_mbps"] = sqlite3_column_double(stmt, 2);
        entry["queue_length"] = sqlite3_column_int64(stmt, 3);
        entry["recorded_at"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        results.push_back(entry);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<json> DatabaseManager::getDevicesJson() {
    std::vector<json> result;
    sqlite3_stmt* stmt = prepareStatement(
        "SELECT device_id, device_type FROM devices ORDER BY device_id;");
    if (stmt == nullptr) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json item;
        item["device_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["device_type"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item["status"] = "online";
        result.push_back(item);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<json> DatabaseManager::getEdgeNodesJson() {
    std::vector<json> result;
    sqlite3_stmt* stmt = prepareStatement(
        "SELECT edge_id, cpu_capacity, ram_capacity_mb, bandwidth_capacity_mbps, latency_ms "
        "FROM edge_nodes ORDER BY edge_id;");
    if (stmt == nullptr) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json item;
        item["edge_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["cpu_capacity"] = sqlite3_column_double(stmt, 1);
        item["ram_capacity_mb"] = sqlite3_column_double(stmt, 2);
        item["bandwidth_capacity_mbps"] = sqlite3_column_double(stmt, 3);
        item["latency_ms"] = sqlite3_column_int64(stmt, 4);
        item["status"] = "online";
        result.push_back(item);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<json> DatabaseManager::getTasksJson(std::size_t limit) {
    std::vector<json> result;
    const std::string sql =
        "SELECT t.task_id, t.device_id, t.sensor_type, t.cpu_percent, t.ram_mb, "
        "t.bandwidth_mbps, t.deadline_ms, t.priority, t.status, t.created_at, "
        "COALESCE((SELECT a.edge_id FROM task_allocations a WHERE a.task_id=t.task_id "
        "AND a.success=1 ORDER BY a.id DESC LIMIT 1), '') AS edge_id, "
        "COALESCE((SELECT a.strategy FROM task_allocations a WHERE a.task_id=t.task_id "
        "AND a.success=1 ORDER BY a.id DESC LIMIT 1), '') AS strategy, "
        "COALESCE((SELECT e.executed_at FROM task_execution e WHERE e.task_id=t.task_id "
        "ORDER BY e.id DESC LIMIT 1), '') AS executed_at "
        "FROM tasks t ORDER BY t.rowid DESC LIMIT ?;";
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (stmt == nullptr) return result;
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(limit));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json item;
        item["task_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["device_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item["sensor_type"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        item["cpu_percent"] = sqlite3_column_double(stmt, 3);
        item["ram_mb"] = sqlite3_column_double(stmt, 4);
        item["bandwidth_mbps"] = sqlite3_column_double(stmt, 5);
        item["deadline_ms"] = sqlite3_column_int64(stmt, 6);
        item["priority"] = sqlite3_column_int(stmt, 7);
        item["status"] = sqlite3_column_int(stmt, 8);
        item["created_at"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        item["edge_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        item["allocation_strategy"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        item["executed_at"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
        result.push_back(item);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<json> DatabaseManager::getActiveTasksJson() {
    std::vector<json> result;
    sqlite3_stmt* stmt = prepareStatement(
        "SELECT t.task_id, t.device_id, t.sensor_type, t.cpu_percent, t.ram_mb, "
        "t.bandwidth_mbps, t.deadline_ms, t.priority, t.status, "
        "COALESCE((SELECT a.edge_id FROM task_allocations a WHERE a.task_id=t.task_id "
        "AND a.success=1 ORDER BY a.id DESC LIMIT 1), '') AS edge_id "
        "FROM tasks t WHERE t.status IN (1,2) ORDER BY t.rowid DESC LIMIT 500;");
    if (stmt == nullptr) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json item;
        item["task_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["device_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item["sensor_type"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        item["cpu_percent"] = sqlite3_column_double(stmt, 3);
        item["ram_mb"] = sqlite3_column_double(stmt, 4);
        item["bandwidth_mbps"] = sqlite3_column_double(stmt, 5);
        item["deadline_ms"] = sqlite3_column_int64(stmt, 6);
        item["priority"] = sqlite3_column_int(stmt, 7);
        item["status"] = sqlite3_column_int(stmt, 8);
        item["edge_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        result.push_back(item);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<json> DatabaseManager::getActivitiesJson(std::size_t limit) {
    std::vector<json> result;
    const std::string sql =
        "SELECT * FROM ("
        "SELECT 'allocation' AS type, 'Task ' || task_id || ' allocated to ' || COALESCE(edge_id,'') AS message, allocated_at AS occurred_at "
        "FROM task_allocations WHERE success=1 "
        "UNION ALL "
        "SELECT 'execution' AS type, 'Task ' || task_id || CASE WHEN success=1 THEN ' completed' ELSE ' failed' END AS message, executed_at AS occurred_at "
        "FROM task_execution "
        ") ORDER BY occurred_at DESC LIMIT ?;";
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (stmt == nullptr) return result;
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(limit));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json item;
        item["type"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["message"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item["occurred_at"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        result.push_back(item);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<json> DatabaseManager::getAlertsJson() {
    std::vector<json> result;
    sqlite3_stmt* stmt = prepareStatement(
        "SELECT edge_id, cpu_percent, ram_mb, queue_length, recorded_at "
        "FROM resource_history WHERE cpu_percent >= 80 "
        "ORDER BY id DESC LIMIT 20;");
    if (stmt == nullptr) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json item;
        item["type"] = "high_cpu";
        item["edge_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["cpu_percent"] = sqlite3_column_double(stmt, 1);
        item["ram_mb"] = sqlite3_column_double(stmt, 2);
        item["queue_length"] = sqlite3_column_int64(stmt, 3);
        item["occurred_at"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        item["message"] = "High CPU usage on " + item["edge_id"].get<std::string>();
        result.push_back(item);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<json> DatabaseManager::getResourceHistoryJson(std::size_t limit) {
    std::vector<json> result;
    sqlite3_stmt* stmt = prepareStatement(
        "SELECT edge_id, cpu_percent, ram_mb, bandwidth_mbps, queue_length, recorded_at "
        "FROM resource_history ORDER BY id DESC LIMIT ?;");
    if (stmt == nullptr) return result;
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(limit));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json item;
        item["edge_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["cpu_percent"] = sqlite3_column_double(stmt, 1);
        item["ram_mb"] = sqlite3_column_double(stmt, 2);
        item["bandwidth_mbps"] = sqlite3_column_double(stmt, 3);
        item["queue_length"] = sqlite3_column_int64(stmt, 4);
        item["recorded_at"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        result.push_back(item);
    }
    sqlite3_finalize(stmt);
    return result;
}

json DatabaseManager::getSystemMetrics() {
    json metrics;

    sqlite3_stmt* stmt = prepareStatement(
        "SELECT "
        "  (SELECT COUNT(*) FROM tasks) AS total_tasks,"
        "  (SELECT COUNT(*) FROM task_allocations WHERE success = 1) AS successful_allocations,"
        "  (SELECT COUNT(*) FROM task_allocations WHERE success = 0) AS failed_allocations,"
        "  (SELECT AVG(execution_time_ms) FROM task_execution) AS avg_execution_ms;");

    if (stmt != nullptr && sqlite3_step(stmt) == SQLITE_ROW) {
        metrics["total_tasks"] = sqlite3_column_int64(stmt, 0);
        metrics["successful_allocations"] = sqlite3_column_int64(stmt, 1);
        metrics["failed_allocations"] = sqlite3_column_int64(stmt, 2);
        metrics["avg_execution_ms"] = sqlite3_column_double(stmt, 3);
    }

    if (stmt != nullptr) {
        sqlite3_finalize(stmt);
    }

    return metrics;
}
