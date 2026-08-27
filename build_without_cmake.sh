#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

JSON_INCLUDE_FLAG=""

if [ -f "/usr/include/nlohmann/json.hpp" ] || [ -f "/usr/local/include/nlohmann/json.hpp" ]; then
    echo "==> Using system-installed nlohmann/json."
elif [ -f "${PROJECT_ROOT}/third_party/json/include/nlohmann/json.hpp" ]; then
    echo "==> Using vendored nlohmann/json from third_party/json/."
    JSON_INCLUDE_FLAG="-I ${PROJECT_ROOT}/third_party/json/include"
else
    echo "ERROR: nlohmann/json.hpp not found." >&2
    echo "  Either install it (nlohmann-json3-dev / json-devel), or fetch" >&2
    echo "  the vendored header — see third_party/json/README.md." >&2
    exit 1
fi

echo "==> Compiling..."

g++ -std=c++17 -O2 -pthread \
    -I include \
    ${JSON_INCLUDE_FLAG} \
    src/main.cpp \
    src/common/logger.cpp \
    src/simulation_config.cpp \
    src/iot_device/iot_device.cpp \
    src/iot_device/sensor_simulator.cpp \
    src/task/task.cpp \
    src/task/task_manager.cpp \
    src/task/task_profile.cpp \
    src/edge/edge_node.cpp \
    src/edge/resource_monitor.cpp \
    src/resource/resource_allocator.cpp \
    src/resource/resource_controller.cpp \
    src/scheduler/task_scheduler.cpp \
    src/database/database_manager.cpp \
    src/executor/task_executor.cpp \
    src/metrics/metrics_collector.cpp \
    -lsqlite3 \
    -o iot_simulator

echo ""
echo "==> Build complete: ./iot_simulator"
echo "Run it: ./iot_simulator --devices 100 --edges 10 --tasks 10000 --compare"
