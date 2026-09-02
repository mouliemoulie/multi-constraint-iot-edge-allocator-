#ifndef IOT_DEVICE_H
#define IOT_DEVICE_H

#include <string>
#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * Base class for all IoT devices in the simulation
 * Represents physical IoT devices like sensors
 */
class IoTDevice {
public:
    enum class DeviceType {
        ULTRASONIC,
        TEMPERATURE,
        CAMERA,
        HUMIDITY,
        PRESSURE
    };

    struct DeviceConfig {
        std::string device_id;
        DeviceType type;
        double data_generation_interval_ms;  // How often data is generated
        double data_variance;                 // Randomness factor
    };

    IoTDevice(const DeviceConfig& config);
    virtual ~IoTDevice() = default;

    // Core interface
    virtual json generateSensorData() = 0;
    std::string getDeviceId() const { return config_.device_id; }
    DeviceType getType() const { return config_.type; }
    std::string getTypeString() const;
    
    // Timing
    bool shouldGenerateData();
    void updateLastGenerationTime();
    std::chrono::system_clock::time_point getLastGenerationTime() const {
        return last_data_generation_;
    }

    // Status
    bool isActive() const { return is_active_; }
    void setActive(bool active) { is_active_ = active; }

protected:
    DeviceConfig config_;
    std::chrono::system_clock::time_point last_data_generation_;
    bool is_active_;

    // Helper functions
    double addVariance(double base_value);
    double generateRandomInRange(double min, double max);
};

#endif // IOT_DEVICE_H
