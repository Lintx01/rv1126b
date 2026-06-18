#include "Interfaces.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#if defined(RV1126B_HAS_MOSQUITTO)
#include <mosquitto.h>
#endif

namespace rv1126b {

struct MqttClient::Impl {
#if defined(RV1126B_HAS_MOSQUITTO)
    mosquitto* client{nullptr};
#endif
    bool library_initialized{false};
};

namespace {

#if defined(RV1126B_HAS_MOSQUITTO)
void onConnect(mosquitto*, void* userdata, int rc) {
    auto* connected = static_cast<std::atomic<bool>*>(userdata);
    if (connected == nullptr) {
        return;
    }

    // callback 运行在 mosquitto 网络线程中，只修改 atomic 状态。
    // 避免在回调里做复杂业务逻辑，防止阻塞 MQTT 网络循环。
    if (rc == MOSQ_ERR_SUCCESS) {
        connected->store(true);
        std::cout << "[MQTT] connected\n";
    } else {
        connected->store(false);
        std::cerr << "[MQTT] connect callback rc=" << rc << "\n";
    }
}

void onDisconnect(mosquitto*, void* userdata, int rc) {
    auto* connected = static_cast<std::atomic<bool>*>(userdata);
    if (connected != nullptr) {
        connected->store(false);
    }

    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MQTT] disconnected unexpectedly, rc=" << rc << "\n";
    } else {
        std::cout << "[MQTT] disconnected\n";
    }
}
#endif

}  // namespace

MqttClient::MqttClient() : impl_(std::make_unique<Impl>()) {}

MqttClient::~MqttClient() {
    disconnect();
}

bool MqttClient::connect(const AppConfig& config) {
    config_ = config;

#if defined(RV1126B_HAS_MOSQUITTO)
    if (!impl_->library_initialized) {
        if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS) {
            std::cerr << "[MQTT] mosquitto_lib_init failed\n";
            return false;
        }
        impl_->library_initialized = true;
    }

    impl_->client = mosquitto_new(config.mqtt_client_id.c_str(), true, &connected_);
    if (impl_->client == nullptr) {
        std::cerr << "[MQTT] mosquitto_new failed\n";
        return false;
    }

    mosquitto_connect_callback_set(impl_->client, onConnect);
    mosquitto_disconnect_callback_set(impl_->client, onDisconnect);

    // 重连策略：网络断开后由 mosquitto 网络线程自动重连。
    // delay 是最小重连间隔，delay*8 是最大重连间隔，true 表示指数退避。
    const int reconnect_delay_sec = std::max(1, config.mqtt_reconnect_delay_ms / 1000);
    mosquitto_reconnect_delay_set(impl_->client, reconnect_delay_sec, reconnect_delay_sec * 8, true);

    int ret = mosquitto_connect_async(
        impl_->client,
        config.mqtt_host.c_str(),
        config.mqtt_port,
        config.mqtt_keepalive_seconds);
    if (ret != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MQTT] connect_async failed: " << mosquitto_strerror(ret) << "\n";
        disconnect();
        return false;
    }

    ret = mosquitto_loop_start(impl_->client);
    if (ret != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MQTT] loop_start failed: " << mosquitto_strerror(ret) << "\n";
        disconnect();
        return false;
    }

    std::cout << "[MQTT] connect async " << config.mqtt_host << ":" << config.mqtt_port
              << ", client_id=" << config.mqtt_client_id << "\n";
    return true;
#else
    // fallback：没有 libmosquitto 时仍保留日志输出，方便在普通开发环境验证业务 JSON。
    connected_ = true;
    std::cout << "[MQTT] log fallback connect " << config.mqtt_host << ":" << config.mqtt_port << "\n";
    return true;
#endif
}

bool MqttClient::publish(const MqttMessage& message) {
#if defined(RV1126B_HAS_MOSQUITTO)
    if (impl_->client == nullptr) {
        return false;
    }
#else
    if (!connected_.load()) {
        return false;
    }
#endif

    for (int attempt = 0; attempt < std::max(1, config_.mqtt_max_publish_retries); ++attempt) {
        if (publishOnce(message)) {
            return true;
        }

        // 发布失败通常来自网络断开或 broker 不可达。
        // 这里做有限重试，不无限阻塞 MQTT 消费线程，避免队列长期堆积。
        (void)reconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.mqtt_reconnect_delay_ms));
    }

    std::cerr << "[MQTT] publish dropped after retries, topic=" << message.topic << "\n";
    return false;
}

bool MqttClient::publishOnce(const MqttMessage& message) {
#if defined(RV1126B_HAS_MOSQUITTO)
    if (impl_->client == nullptr) {
        return false;
    }

    int message_id = 0;
    const int ret = mosquitto_publish(
        impl_->client,
        &message_id,
        message.topic.c_str(),
        static_cast<int>(message.payload.size()),
        message.payload.data(),
        message.qos,
        message.retained);
    if (ret != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MQTT] publish failed: " << mosquitto_strerror(ret)
                  << ", topic=" << message.topic << "\n";
        connected_ = false;
        return false;
    }

    // QoS 说明：
    // QoS 0：最多一次，适合高频 AI 结果。
    // QoS 1：至少一次，适合告警/提醒，但网页端需按 frame_id 或时间戳去重。
    // retain：保留最后一条状态，适合设备在线状态，不建议用于高频识别结果。
    std::cout << "[MQTT] publish mid=" << message_id << ", topic=" << message.topic << "\n";
    connected_ = true;
    return true;
#else
    std::cout << "[MQTT] topic=" << message.topic << ", payload=" << message.payload << "\n";
    return true;
#endif
}

bool MqttClient::reconnect() {
#if defined(RV1126B_HAS_MOSQUITTO)
    if (impl_->client == nullptr) {
        return false;
    }

    const int ret = mosquitto_reconnect_async(impl_->client);
    if (ret != MOSQ_ERR_SUCCESS && ret != MOSQ_ERR_INVAL) {
        std::cerr << "[MQTT] reconnect_async failed: " << mosquitto_strerror(ret) << "\n";
        connected_ = false;
        return false;
    }

    connected_ = true;
    return true;
#else
    connected_ = true;
    return true;
#endif
}

void MqttClient::disconnect() {
#if defined(RV1126B_HAS_MOSQUITTO)
    if (impl_ && impl_->client != nullptr) {
        mosquitto_disconnect(impl_->client);
        mosquitto_loop_stop(impl_->client, true);
        mosquitto_destroy(impl_->client);
        impl_->client = nullptr;
    }
    if (impl_ && impl_->library_initialized) {
        mosquitto_lib_cleanup();
        impl_->library_initialized = false;
    }
#endif

    if (connected_.exchange(false)) {
        std::cout << "[MQTT] disconnect\n";
    }
}

}  // namespace rv1126b
