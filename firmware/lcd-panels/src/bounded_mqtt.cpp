#include "bounded_mqtt.h"

#include <lwip/sockets.h>

#include <cerrno>
#include <cstring>

namespace {
bool appendByte(uint8_t* output, size_t capacity, size_t& used, uint8_t value) {
    if (used >= capacity) return false;
    output[used++] = value;
    return true;
}

bool appendString(uint8_t* output, size_t capacity, size_t& used, const char* value) {
    const size_t length = std::strlen(value);
    if (length > 0xffffU || used + 2U + length > capacity) return false;
    output[used++] = static_cast<uint8_t>(length >> 8U);
    output[used++] = static_cast<uint8_t>(length & 0xffU);
    std::memcpy(output + used, value, length);
    used += length;
    return true;
}

size_t encodeRemaining(uint8_t* output, size_t value) {
    size_t used = 0U;
    do {
        uint8_t encoded = static_cast<uint8_t>(value % 128U);
        value /= 128U;
        if (value != 0U) encoded |= 0x80U;
        output[used++] = encoded;
    } while (value != 0U && used < 4U);
    return used;
}
}

void BoundedMqttClient::DeadlineClient::startDeadline(uint32_t duration_ms) {
    started_ms_ = millis(); duration_ms_ = duration_ms; active_ = true;
}
void BoundedMqttClient::DeadlineClient::clearDeadline() { active_ = false; }
uint32_t BoundedMqttClient::DeadlineClient::remainingMs() const {
    if (!active_) return 0U;
    const uint32_t elapsed = static_cast<uint32_t>(millis() - started_ms_);
    return elapsed >= duration_ms_ ? 0U : duration_ms_ - elapsed;
}
int BoundedMqttClient::DeadlineClient::connect(IPAddress ip, uint16_t port) {
    const uint32_t remaining = remainingMs();
    return remaining == 0U ? 0 : WiFiClient::connect(ip, port, static_cast<int32_t>(remaining));
}
int BoundedMqttClient::DeadlineClient::connect(const char*, uint16_t) { return 0; }
size_t BoundedMqttClient::DeadlineClient::write(uint8_t value) { return write(&value, 1U); }
size_t BoundedMqttClient::DeadlineClient::write(const uint8_t* buffer, size_t size) {
    size_t sent = 0U;
    while (sent < size) {
        const uint32_t remaining = remainingMs();
        const int socket_fd = fd();
        if (remaining == 0U || socket_fd < 0) { WiFiClient::stop(); break; }
        fd_set writable; FD_ZERO(&writable); FD_SET(socket_fd, &writable);
        timeval timeout{static_cast<time_t>(remaining / 1000U), static_cast<suseconds_t>((remaining % 1000U) * 1000U)};
        const int ready = select(socket_fd + 1, nullptr, &writable, nullptr, &timeout);
        if (ready == 0) { WiFiClient::stop(); break; }
        if (ready < 0) { if (errno == EINTR) continue; WiFiClient::stop(); break; }
        const int result = send(socket_fd, buffer + sent, size - sent, MSG_DONTWAIT);
        if (result > 0) sent += static_cast<size_t>(result);
        else if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
        else { WiFiClient::stop(); break; }
    }
    return sent;
}

bool BoundedMqttClient::readPacketBlocking(uint8_t& type, uint8_t* body, size_t capacity, size_t& length) {
    auto readByte = [this](uint8_t& output) {
        while (network_.remainingMs() != 0U) {
            if (network_.available()) { const int value = network_.read(); if (value >= 0) { output = static_cast<uint8_t>(value); return true; } }
            delay(1);
        }
        return false;
    };
    if (!readByte(type)) return false;
    size_t remaining = 0U; uint32_t multiplier = 1U;
    for (unsigned index = 0U; index < 4U; ++index) {
        uint8_t encoded = 0U;
        if (!readByte(encoded)) return false;
        remaining += static_cast<size_t>(encoded & 0x7fU) * multiplier;
        if (remaining > capacity) return false;
        if ((encoded & 0x80U) == 0U) {
            if (index != 0U && encoded == 0U) return false;
            length = remaining;
            for (size_t body_index = 0U; body_index < length; ++body_index) if (!readByte(body[body_index])) return false;
            return true;
        }
        if (index == 3U) return false;
        multiplier *= 128U;
    }
    return false;
}

bool BoundedMqttClient::connect(const PanelConfig& config, PanelMqttMessageHandler handler) {
    stop();
    IPAddress address;
    if (!address.fromString(config.mqtt_host)) return false;
    network_.startDeadline(6000U);
    if (!network_.connect(address, config.mqtt_port)) { stop(); return false; }

    uint8_t variable[220]{}; size_t variable_used = 0U;
    const uint8_t protocol[] = {0x00U, 0x04U, 'M', 'Q', 'T', 'T', 0x04U};
    std::memcpy(variable, protocol, sizeof(protocol)); variable_used = sizeof(protocol);
    uint8_t flags = 0x02U;
    if (config.mqtt_username[0] != '\0') flags |= 0x80U;
    if (config.mqtt_password[0] != '\0') flags |= 0x40U;
    if (!appendByte(variable, sizeof(variable), variable_used, flags)
        || !appendByte(variable, sizeof(variable), variable_used, 0x00U)
        || !appendByte(variable, sizeof(variable), variable_used, 60U)) { stop(); return false; }
    uint8_t mac[6]{}; WiFi.macAddress(mac);
    char client_id[32]{};
    std::snprintf(client_id, sizeof(client_id), "lcd-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (!appendString(variable, sizeof(variable), variable_used, client_id)
        || (config.mqtt_username[0] != '\0' && !appendString(variable, sizeof(variable), variable_used, config.mqtt_username))
        || (config.mqtt_password[0] != '\0' && !appendString(variable, sizeof(variable), variable_used, config.mqtt_password))) { stop(); return false; }
    uint8_t packet[224]{}; size_t used = 0U;
    packet[used++] = 0x10U; used += encodeRemaining(packet + used, variable_used);
    std::memcpy(packet + used, variable, variable_used); used += variable_used;
    if (network_.write(packet, used) != used) { stop(); return false; }
    uint8_t response_type = 0U; uint8_t response[8]{}; size_t response_length = 0U;
    if (!readPacketBlocking(response_type, response, sizeof(response), response_length)
        || response_type != 0x20U || response_length != 2U || response[0] != 0U || response[1] != 0U) { stop(); return false; }

    variable_used = 0U;
    appendByte(variable, sizeof(variable), variable_used, 0x00U); appendByte(variable, sizeof(variable), variable_used, 0x01U);
    if (!appendString(variable, sizeof(variable), variable_used, config.mqtt_topic)
        || !appendByte(variable, sizeof(variable), variable_used, 0x00U)) { stop(); return false; }
    used = 0U; packet[used++] = 0x82U; used += encodeRemaining(packet + used, variable_used);
    std::memcpy(packet + used, variable, variable_used); used += variable_used;
    if (network_.write(packet, used) != used) { stop(); return false; }
    if (!readPacketBlocking(response_type, response, sizeof(response), response_length)
        || response_type != 0x90U || response_length != 3U || response[0] != 0U || response[1] != 1U || response[2] != 0U) { stop(); return false; }
    network_.clearDeadline();
    std::strcpy(topic_, config.mqtt_topic);
    handler_ = handler;
    last_activity_ms_ = millis();
    resetPacket();
    return true;
}

bool BoundedMqttClient::sendPacket(const uint8_t* data, size_t length, uint32_t timeout_ms) {
    network_.startDeadline(timeout_ms);
    const bool sent = network_.write(data, length) == length;
    network_.clearDeadline();
    if (!sent) stop();
    return sent;
}

void BoundedMqttClient::resetPacket() {
    packet_type_ = 0U; body_length_ = 0U; remaining_length_ = 0U; multiplier_ = 1U;
    remaining_bytes_ = 0U; reading_remaining_ = false; packet_active_ = false; packet_started_ms_ = 0U;
}

void BoundedMqttClient::consume(uint8_t value) {
    if (!packet_active_) { packet_active_ = true; packet_started_ms_ = millis(); packet_type_ = value; reading_remaining_ = true; return; }
    if (reading_remaining_) {
        if (++remaining_bytes_ > 4U) { stop(); return; }
        remaining_length_ += static_cast<size_t>(value & 0x7fU) * multiplier_;
        if (remaining_length_ > sizeof(body_)) { stop(); return; }
        if ((value & 0x80U) != 0U) {
            if (remaining_bytes_ == 4U) { stop(); return; }
            multiplier_ *= 128U; return;
        }
        if (remaining_bytes_ > 1U && value == 0U) { stop(); return; }
        reading_remaining_ = false;
        if (remaining_length_ == 0U) { processPacket(); resetPacket(); }
        return;
    }
    body_[body_length_++] = value;
    if (body_length_ == remaining_length_) { processPacket(); resetPacket(); }
}

void BoundedMqttClient::processPacket() {
    last_activity_ms_ = millis(); awaiting_ping_ = false;
    if (packet_type_ == 0xd0U && body_length_ == 0U) return;
    if ((packet_type_ & 0xf0U) != 0x30U || (packet_type_ & 0x0eU) != 0U || body_length_ < 2U) { stop(); return; }
    const size_t topic_length = (static_cast<size_t>(body_[0]) << 8U) | body_[1];
    if (topic_length == 0U || topic_length > 128U || 2U + topic_length > body_length_) { stop(); return; }
    const size_t payload_length = body_length_ - 2U - topic_length;
    if (std::strlen(topic_) != topic_length || std::memcmp(body_ + 2U, topic_, topic_length) != 0 || payload_length == 0U || payload_length > 256U) return;
    if (handler_ != nullptr) handler_(body_ + 2U + topic_length, payload_length);
}

void BoundedMqttClient::loop() {
    if (!network_.connected()) return;
    const uint32_t now = millis();
    if (packet_active_ && static_cast<uint32_t>(now - packet_started_ms_) > 2000U) { stop(); return; }
    unsigned processed = 0U;
    while (network_.available() && processed++ < 512U && network_.connected()) {
        const int value = network_.read(); if (value < 0) break; consume(static_cast<uint8_t>(value));
    }
    if (!network_.connected()) return;
    if (awaiting_ping_ && static_cast<uint32_t>(now - ping_sent_ms_) > 10000U) { stop(); return; }
    if (!awaiting_ping_ && static_cast<uint32_t>(now - last_activity_ms_) >= 30000U) {
        static constexpr uint8_t ping[] = {0xc0U, 0x00U};
        if (sendPacket(ping, sizeof(ping), 1000U)) { awaiting_ping_ = true; ping_sent_ms_ = now; }
    }
}

void BoundedMqttClient::stop() {
    network_.stop(); network_.clearDeadline(); handler_ = nullptr; topic_[0] = '\0'; awaiting_ping_ = false; resetPacket();
}
bool BoundedMqttClient::connected() { return network_.connected(); }
