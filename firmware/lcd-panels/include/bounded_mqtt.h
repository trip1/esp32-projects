#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "panel_logic.h"

using PanelMqttMessageHandler = void (*)(const uint8_t* payload, size_t length);

class BoundedMqttClient {
public:
    bool connect(const PanelConfig& config, PanelMqttMessageHandler handler);
    void loop();
    void stop();
    bool connected();

private:
    class DeadlineClient : public WiFiClient {
    public:
        void startDeadline(uint32_t duration_ms);
        void clearDeadline();
        uint32_t remainingMs() const;
        int connect(IPAddress ip, uint16_t port) override;
        int connect(const char*, uint16_t) override;
        size_t write(uint8_t value) override;
        size_t write(const uint8_t* buffer, size_t size) override;
    private:
        bool active_ = false;
        uint32_t started_ms_ = 0U;
        uint32_t duration_ms_ = 0U;
    };

    bool sendPacket(const uint8_t* data, size_t length, uint32_t timeout_ms);
    bool readPacketBlocking(uint8_t& type, uint8_t* body, size_t capacity, size_t& length);
    void consume(uint8_t value);
    void processPacket();
    void resetPacket();

    DeadlineClient network_;
    PanelMqttMessageHandler handler_ = nullptr;
    char topic_[129]{};
    uint8_t packet_type_ = 0U;
    uint8_t body_[400]{};
    size_t body_length_ = 0U;
    size_t remaining_length_ = 0U;
    uint32_t multiplier_ = 1U;
    uint8_t remaining_bytes_ = 0U;
    bool reading_remaining_ = false;
    bool packet_active_ = false;
    uint32_t packet_started_ms_ = 0U;
    uint32_t last_activity_ms_ = 0U;
    uint32_t ping_sent_ms_ = 0U;
    bool awaiting_ping_ = false;
};
