#ifndef SIMULATION_CONFIG_H
#define SIMULATION_CONFIG_H

#include <string>
#include <cstdint>

/**
 * Central place for command-line-configurable simulation parameters.
 * Keeps main.cpp free of ad-hoc argv parsing scattered through logic.
 */
struct SimulationConfig {
    std::size_t device_count = 100U;
    std::size_t edge_node_count = 10U;
    std::size_t task_count = 10000U;
    std::string strategy = "multi_constraint";  // round_robin | least_load | priority_based | multi_constraint
    std::string scheduling = "priority_based";  // fifo | priority_based | edf | round_robin
    std::string db_path = "data/simulation.db";
    std::string log_path = "logs/simulation.log";
    bool use_mqtt = false;  // Off by default: requires a running broker.
    std::string mqtt_broker = "tcp://localhost:1883";
    bool run_comparison = false;  // Spec §18: run all 4 strategies back-to-back.
    bool verbose = false;         // Print task generation, allocation, scheduling and execution trace.
    bool web_mode = false;         // Keep a live HTTP API available for the dashboard.
    uint16_t web_port = 8080U;
    bool automatic_mode = false;  // Let the runtime recommend allocation/scheduling strategies.

    static SimulationConfig parseArgs(int argc, char** argv);
    static void printUsage(const std::string& program_name);
};

#endif  // SIMULATION_CONFIG_H
