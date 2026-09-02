#include "simulation_config.h"

#include <iostream>
#include <cstring>

namespace {

bool matchesFlag(const char* arg, const char* short_flag, const char* long_flag) {
    return (std::strcmp(arg, short_flag) == 0) || (std::strcmp(arg, long_flag) == 0);
}

}  // namespace

void SimulationConfig::printUsage(const std::string& program_name) {
    std::cout <<
        "Usage: " << program_name << " [options]\n"
        "\n"
        "Options:\n"
        "  -d, --devices <N>       Number of simulated IoT devices (default: 100)\n"
        "  -e, --edges <N>         Number of simulated edge nodes (default: 10)\n"
        "  -t, --tasks <N>         Number of tasks to generate (default: 10000)\n"
        "  -s, --strategy <name>   round_robin | least_load | priority_based | multi_constraint\n"
        "                          (default: multi_constraint)\n"
        "  -c, --compare           Run all 4 strategies back-to-back and print a comparison table\n"
        "      --mqtt              Enable MQTT publishing (requires a running broker)\n"
        "      --broker <addr>     MQTT broker address (default: tcp://localhost:1883)\n"
        "      --db <path>         SQLite database path (default: data/simulation.db)\n"
        "      --verbose           Enable detailed task lifecycle trace\n"
        "  -h, --help              Show this message\n";
}

SimulationConfig SimulationConfig::parseArgs(int argc, char** argv) {
    SimulationConfig config;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const bool has_next = (i + 1) < argc;

        if (matchesFlag(arg, "-d", "--devices") && has_next) {
            config.device_count = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (matchesFlag(arg, "-e", "--edges") && has_next) {
            config.edge_node_count = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (matchesFlag(arg, "-t", "--tasks") && has_next) {
            config.task_count = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (matchesFlag(arg, "-s", "--strategy") && has_next) {
            config.strategy = argv[++i];
        } else if (std::strcmp(arg, "--scheduling") == 0 && has_next) {
            config.scheduling = argv[++i];
        } else if (matchesFlag(arg, "-c", "--compare")) {
            config.run_comparison = true;
        } else if (std::strcmp(arg, "--mqtt") == 0) {
            config.use_mqtt = true;
        } else if (std::strcmp(arg, "--broker") == 0 && has_next) {
            config.mqtt_broker = argv[++i];
        } else if (std::strcmp(arg, "--db") == 0 && has_next) {
            config.db_path = argv[++i];
        } else if (std::strcmp(arg, "--verbose") == 0) {
            config.verbose = true;
        } else if (std::strcmp(arg, "--web") == 0) {
            config.web_mode = true;
        } else if (std::strcmp(arg, "--web-port") == 0 && has_next) {
            config.web_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(arg, "--automatic") == 0) {
            config.automatic_mode = true;
        } else if (matchesFlag(arg, "-h", "--help")) {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            std::exit(1);
        }
    }

    return config;
}
