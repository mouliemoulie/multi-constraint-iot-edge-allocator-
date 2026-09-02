#include "iot_device/iot_device.h"

#include <random>
#include <chrono>

IoTDevice::IoTDevice(const DeviceConfig& config)
    : config_(config),
      last_data_generation_(std::chrono::system_clock::now()),
      is_active_(true) {
}

std::string IoTDevice::getTypeString() const {
    std::string result;
    switch (config_.type) {
        case DeviceType::ULTRASONIC:
            result = "ultrasonic";
            break;
        case DeviceType::TEMPERATURE:
            result = "temperature";
            break;
        case DeviceType::CAMERA:
            result = "camera";
            break;
        case DeviceType::HUMIDITY:
            result = "humidity";
            break;
        case DeviceType::PRESSURE:
            result = "pressure";
            break;
        default:
            result = "unknown";
            break;
    }
    return result;
}

bool IoTDevice::shouldGenerateData() {
    if (!is_active_) {
        return false;
    }

    const auto now = std::chrono::system_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_data_generation_).count();

    return static_cast<double>(elapsed_ms) >= config_.data_generation_interval_ms;
}

void IoTDevice::updateLastGenerationTime() {
    last_data_generation_ = std::chrono::system_clock::now();
}

double IoTDevice::generateRandomInRange(double min, double max) {
    // Thread-local RNG: each device-generation thread gets its own
    // independent engine, avoiding a shared-state data race without
    // needing a mutex on the hot path.
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_real_distribution<double> distribution(min, max);
    return distribution(generator);
}

double IoTDevice::addVariance(double base_value) {
    const double variance_range = base_value * config_.data_variance;
    const double offset = generateRandomInRange(-variance_range, variance_range);
    return base_value + offset;
}
