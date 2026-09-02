#!/usr/bin/env bash
#
# One-shot dependency install + configure + build for Debian/Ubuntu.
# See docs/BUILD.md for manual steps, other platforms, and Valgrind /
# sanitizer usage.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/build"

echo "==> Installing dependencies (requires sudo)..."
sudo apt-get update
sudo apt-get install -y \
    cmake g++ libsqlite3-dev nlohmann-json3-dev \
    libgtest-dev valgrind lcov

if ! ldconfig -p | grep -q libpaho-mqttpp3; then
    echo "==> NOTE: Paho MQTT C++ client not detected."
    echo "    The build will proceed with MQTT support disabled."
    echo "    See docs/BUILD.md for build-from-source instructions if you need it."
fi

echo "==> Configuring (Debug build)..."
cmake -B "$BUILD_DIR" -S "$PROJECT_ROOT" -DCMAKE_BUILD_TYPE=Debug

echo "==> Building..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "==> Running unit tests..."
(cd "$BUILD_DIR" && ctest --output-on-failure)

echo ""
echo "==> Done. Try it:"
echo "    ${BUILD_DIR}/iot_simulator --devices 100 --edges 10 --tasks 10000 --compare"
echo ""
echo "==> For a code coverage report (separate build dir):"
echo "    cmake -B ${PROJECT_ROOT}/build-coverage -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON"
echo "    cmake --build ${PROJECT_ROOT}/build-coverage -j\$(nproc)"
echo "    cmake --build ${PROJECT_ROOT}/build-coverage --target coverage"
