#include "iot_device/sensor_simulator.h"

#include <chrono>
#include <ctime>
#include <array>
#include <string>

namespace {

/**
 * Formats the current wall-clock time as HH:MM:SS for embedding in
 * sensor payloads (matches the format shown in the project spec's
 * example ultrasonic MQTT message).
 */
std::string currentTimeString() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&now_c, &tm_buf);

    std::array<char, 16> buffer{};
    const std::size_t written = std::strftime(
        buffer.data(), buffer.size(), "%H:%M:%S", &tm_buf);
    if (written == 0U) {
        return std::string("00:00:00");
    }
    return std::string(buffer.data());
}

}  // namespace

// ============================================================
// UltrasonicSensor
// ============================================================

UltrasonicSensor::UltrasonicSensor(const DeviceConfig& config)
    : IoTDevice(config),
      min_distance_cm_(5.0),
      max_distance_cm_(500.0),
      current_distance_((min_distance_cm_ + max_distance_cm_) / 2.0) {
}

json UltrasonicSensor::generateSensorData() {
    current_distance_ = addVariance(current_distance_);
    if (current_distance_ < min_distance_cm_) {
        current_distance_ = min_distance_cm_;
    }
    if (current_distance_ > max_distance_cm_) {
        current_distance_ = max_distance_cm_;
    }

    json payload;
    payload["device_id"] = getDeviceId();
    payload["sensor_type"] = "ultrasonic";
    payload["distance_cm"] = current_distance_;
    payload["timestamp"] = currentTimeString();
    return payload;
}

// ============================================================
// TemperatureSensor
// ============================================================

TemperatureSensor::TemperatureSensor(const DeviceConfig& config)
    : IoTDevice(config),
      current_temperature_(22.0) {  // room-temperature baseline
}

json TemperatureSensor::generateSensorData() {
    current_temperature_ = addVariance(current_temperature_);
    if (current_temperature_ < min_temp_celsius_) {
        current_temperature_ = min_temp_celsius_;
    }
    if (current_temperature_ > max_temp_celsius_) {
        current_temperature_ = max_temp_celsius_;
    }

    json payload;
    payload["device_id"] = getDeviceId();
    payload["sensor_type"] = "temperature";
    payload["temperature_celsius"] = current_temperature_;
    payload["timestamp"] = currentTimeString();
    return payload;
}

// ============================================================
// CameraSensor
// ============================================================

CameraSensor::CameraSensor(const DeviceConfig& config)
    : IoTDevice(config),
      frame_count_(0U) {
}

json CameraSensor::generateSensorData() {
    ++frame_count_;

    json payload;
    payload["device_id"] = getDeviceId();
    payload["sensor_type"] = "camera";
    payload["frame_number"] = frame_count_;
    payload["resolution_width"] = resolution_width_;
    payload["resolution_height"] = resolution_height_;
    payload["timestamp"] = currentTimeString();
    return payload;
}

// ============================================================
// HumiditySensor
// ============================================================

HumiditySensor::HumiditySensor(const DeviceConfig& config)
    : IoTDevice(config),
      current_humidity_(50.0) {
}

json HumiditySensor::generateSensorData() {
    current_humidity_ = addVariance(current_humidity_);
    if (current_humidity_ < min_humidity_) {
        current_humidity_ = min_humidity_;
    }
    if (current_humidity_ > max_humidity_) {
        current_humidity_ = max_humidity_;
    }

    json payload;
    payload["device_id"] = getDeviceId();
    payload["sensor_type"] = "humidity";
    payload["humidity_percent"] = current_humidity_;
    payload["timestamp"] = currentTimeString();
    return payload;
}

// ============================================================
// PressureSensor
// ============================================================

PressureSensor::PressureSensor(const DeviceConfig& config)
    : IoTDevice(config),
      min_pressure_hpa_(950.0),
      max_pressure_hpa_(1050.0),
      current_pressure_((min_pressure_hpa_ + max_pressure_hpa_) / 2.0) {
}

json PressureSensor::generateSensorData() {
    current_pressure_ = addVariance(current_pressure_);
    if (current_pressure_ < min_pressure_hpa_) {
        current_pressure_ = min_pressure_hpa_;
    }
    if (current_pressure_ > max_pressure_hpa_) {
        current_pressure_ = max_pressure_hpa_;
    }

    json payload;
    payload["device_id"] = getDeviceId();
    payload["sensor_type"] = "pressure";
    payload["pressure_hpa"] = current_pressure_;
    payload["timestamp"] = currentTimeString();
    return payload;
}
