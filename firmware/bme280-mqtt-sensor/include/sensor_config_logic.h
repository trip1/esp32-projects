#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

bool isValidWifiSsid(const std::string& value);
bool isValidWifiPassword(const std::string& value);
bool isValidMqttHost(const std::string& value);
bool isValidOptionalCredential(const std::string& value, std::size_t maximum_bytes);
bool isValidTopicPrefix(const std::string& value);
bool isValidSleepMinutes(std::uint32_t value);
bool isValidBme280Reading(float temperature_c, float humidity_percent, float pressure_hpa);
bool parseBoundedHttpHeaders(
    const char* data,
    std::size_t length,
    std::size_t maximum_body_bytes,
    std::size_t& content_length,
    bool& has_content_length);
bool parseBoundedHttpRequest(
    const char* data,
    std::size_t length,
    std::size_t maximum_body_bytes,
    char* method,
    std::size_t method_capacity,
    char* target,
    std::size_t target_capacity,
    std::size_t& content_length,
    bool& has_content_length);
bool shouldProcessPending(bool pending_record_valid, bool rejected_pending_retained);
std::uint32_t sleepMinutesAfterPendingAttempt(
    bool pending,
    bool published,
    bool promoted,
    bool active_ready,
    std::uint32_t candidate_minutes,
    std::uint32_t active_minutes,
    std::uint32_t fallback_minutes);
std::uint32_t configurationCrc32(const std::uint8_t* data, std::size_t length);
