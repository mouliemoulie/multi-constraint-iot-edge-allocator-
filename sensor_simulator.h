#ifndef SENSOR_SIMULATOR_H
#define SENSOR_SIMULATOR_H

#include "iot_device.h"

/**
 * Ultrasonic sensor simulator
 * Measures distance to objects
 */
class UltrasonicSensor : public IoTDevice {
public:
    UltrasonicSensor(const DeviceConfig& config);
    json generateSensorData() override;

private:
    // Declaration order matters: members initialize in this order
    // regardless of member-initializer-list order, so the bounds must
    // be declared before current_distance_, which depends on them.
    // Initialized in the constructor's initializer list (not here) to
    // keep the single source of truth in one place and avoid
    // -Wreorder warnings.
    double min_distance_cm_;
    double max_distance_cm_;
    double current_distance_;
};

/**
 * Temperature sensor simulator
 * Measures ambient temperature
 */
class TemperatureSensor : public IoTDevice {
public:
    TemperatureSensor(const DeviceConfig& config);
    json generateSensorData() override;

private:
    double current_temperature_;
    double min_temp_celsius_ = -50.0;
    double max_temp_celsius_ = 100.0;
};

/**
 * Camera simulator
 * Simulates video frame data with resolution and frame count
 */
class CameraSensor : public IoTDevice {
public:
    CameraSensor(const DeviceConfig& config);
    json generateSensorData() override;

private:
    uint64_t frame_count_;
    int resolution_width_ = 1920;
    int resolution_height_ = 1080;
};

/**
 * Humidity sensor simulator
 * Measures relative humidity
 */
class HumiditySensor : public IoTDevice {
public:
    HumiditySensor(const DeviceConfig& config);
    json generateSensorData() override;

private:
    double current_humidity_;
    double min_humidity_ = 0.0;
    double max_humidity_ = 100.0;
};

/**
 * Pressure sensor simulator
 * Measures atmospheric pressure
 */
class PressureSensor : public IoTDevice {
public:
    PressureSensor(const DeviceConfig& config);
    json generateSensorData() override;

private:
    // See UltrasonicSensor above: declaration order determines
    // initialization order, so bounds are declared first and
    // initialized via the constructor's initializer list.
    double min_pressure_hpa_;
    double max_pressure_hpa_;
    double current_pressure_;
};

#endif // SENSOR_SIMULATOR_H
