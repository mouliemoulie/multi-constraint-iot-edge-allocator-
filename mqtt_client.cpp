#include "mqtt/mqtt_client.h"
#include "common/logger.h"

// Paho MQTT C++ headers. Only included here — never in the public header —
// so consumers of MqttClient never need Paho on their include path.
// exception.h is included explicitly rather than relying on it being
// pulled in transitively by async_client.h, since that's an
// implementation detail of Paho that could change between versions.
#include <mqtt/async_client.h>
#include <mqtt/connect_options.h>
#include <mqtt/message.h>
#include <mqtt/callback.h>
#include <mqtt/exception.h>

namespace {
constexpr int kDefaultQos = 1;
constexpr int kConnectTimeoutSeconds = 10;
}  // namespace

/**
 * Internal Paho callback adapter. Kept private to the .cpp file: the
 * public MqttClient header never exposes Paho's callback interface.
 */
class MqttClient::MessageCallbackAdapter : public virtual mqtt::callback {
public:
    explicit MessageCallbackAdapter(MqttClient& owner) : owner_(owner) {}

    void message_arrived(mqtt::const_message_ptr msg) override {
        owner_.onMessageArrived(msg->get_topic(), msg->to_string());
    }

    void connection_lost(const std::string& cause) override {
        Logger::getInstance().warn("MqttClient", "Connection lost: " + cause);
    }

    void delivery_complete(mqtt::delivery_token_ptr /*token*/) override {
        // No-op: publish() already tracks success synchronously via wait().
    }

private:
    MqttClient& owner_;
};

MqttClient::MqttClient(const std::string& broker_address, const std::string& client_id)
    : broker_address_(broker_address),
      client_id_(client_id),
      client_(nullptr),
      messages_published_(0U),
      messages_received_(0U) {
    client_ = std::make_unique<mqtt::async_client>(broker_address_, client_id_);
}

MqttClient::~MqttClient() {
    // Ensure the network connection is torn down before the client
    // object is destroyed, avoiding a dangling-connection leak.
    disconnect();
}

bool MqttClient::connect() {
    std::lock_guard<std::mutex> lock(client_mutex_);

    if (client_ == nullptr) {
        return false;
    }

    try {
        mqtt::connect_options options;
        options.set_keep_alive_interval(20);
        options.set_clean_session(true);
        options.set_connect_timeout(kConnectTimeoutSeconds);

        mqtt::token_ptr conn_token = client_->connect(options);
        conn_token->wait();
        return client_->is_connected();
    } catch (const mqtt::exception& e) {
        // Connection failures are treated as recoverable at the call
        // site (caller decides retry policy) — this is the one
        // documented exception boundary per MISRA deviation D2.
        Logger::getInstance().error("MqttClient", std::string("Connect failed: ") + e.what());
        return false;
    }
}

void MqttClient::disconnect() {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_ != nullptr && client_->is_connected()) {
        try {
            client_->disconnect()->wait();
        } catch (const mqtt::exception& e) {
            Logger::getInstance().warn("MqttClient", std::string("Disconnect error: ") + e.what());
        }
    }
}

bool MqttClient::isConnected() const {
    std::lock_guard<std::mutex> lock(client_mutex_);
    return (client_ != nullptr) && client_->is_connected();
}

bool MqttClient::publish(const std::string& topic, const json& payload, int qos) {
    std::lock_guard<std::mutex> lock(client_mutex_);

    if (client_ == nullptr || !client_->is_connected()) {
        return false;
    }

    try {
        const std::string serialized = payload.dump();
        mqtt::message_ptr msg = mqtt::make_message(topic, serialized);
        msg->set_qos((qos > 0) ? qos : kDefaultQos);

        client_->publish(msg)->wait();
        ++messages_published_;
        return true;
    } catch (const mqtt::exception& e) {
        Logger::getInstance().error("MqttClient", std::string("Publish failed: ") + e.what());
        return false;
    }
}

bool MqttClient::subscribe(const std::string& topic, int qos) {
    std::lock_guard<std::mutex> lock(client_mutex_);

    if (client_ == nullptr || !client_->is_connected()) {
        return false;
    }

    try {
        client_->subscribe(topic, (qos > 0) ? qos : kDefaultQos)->wait();
        return true;
    } catch (const mqtt::exception& e) {
        Logger::getInstance().error("MqttClient", std::string("Subscribe failed: ") + e.what());
        return false;
    }
}

void MqttClient::registerMessageCallback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    message_callback_ = std::move(callback);

    if (client_ != nullptr) {
        // Ownership stays with MqttClient via unique_ptr; Paho only
        // borrows a raw observer pointer, which is standard practice
        // for its callback registration API.
        static MessageCallbackAdapter adapter(*this);
        client_->set_callback(adapter);
    }
}

void MqttClient::onMessageArrived(const std::string& topic, const std::string& raw_payload) {
    ++messages_received_;

    if (!message_callback_) {
        return;
    }

    try {
        const json payload = json::parse(raw_payload);
        message_callback_(topic, payload);
    } catch (const json::parse_error& e) {
        Logger::getInstance().warn("MqttClient",
            "Malformed JSON payload on topic " + topic + ": " + e.what());
    }
}
