#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>

// Forward-declare the Paho C++ client type so this header does not force
// every translation unit that includes it to also pull in the Paho
// headers. The real type is only named in mqtt_client.cpp.
namespace mqtt {
class async_client;
}

using json = nlohmann::json;

/**
 * Thin RAII wrapper around the Paho MQTT C++ client.
 *
 * MISRA deviation D5 (see docs/MISRA_DEVIATIONS.md): the underlying Paho
 * client is a third-party C++ API. Ownership of the underlying client
 * pointer never leaves this class.
 *
 * Per spec §11: "MQTT does not perform resource allocation. It only
 * transports messages." This class is intentionally dumb: connect,
 * publish, subscribe, dispatch to callback. No business logic lives here.
 */
class MqttClient {
public:
    using MessageCallback = std::function<void(const std::string& topic, const json& payload)>;

    MqttClient(const std::string& broker_address, const std::string& client_id);
    ~MqttClient();

    // Non-copyable: owns a live network connection.
    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;

    bool connect();
    void disconnect();
    bool isConnected() const;

    bool publish(const std::string& topic, const json& payload, int qos = 1);
    bool subscribe(const std::string& topic, int qos = 1);

    void registerMessageCallback(MessageCallback callback);

    // Statistics
    uint64_t getMessagesPublished() const { return messages_published_; }
    uint64_t getMessagesReceived() const { return messages_received_; }

private:
    // Defined only in mqtt_client.cpp, where the real Paho callback
    // interface is visible. Kept private so the Paho callback ABI never
    // leaks into this public header.
    class MessageCallbackAdapter;

    std::string broker_address_;
    std::string client_id_;
    std::unique_ptr<mqtt::async_client> client_;
    MessageCallback message_callback_;
    mutable std::mutex client_mutex_;
    uint64_t messages_published_;
    uint64_t messages_received_;

    void onMessageArrived(const std::string& topic, const std::string& raw_payload);
};

#endif  // MQTT_CLIENT_H
