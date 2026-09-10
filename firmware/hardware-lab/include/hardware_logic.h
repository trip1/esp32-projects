#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

float distanceCentimetersFromEcho(std::uint32_t echo_microseconds);
float medianDistance(const float* values, std::size_t count);
bool occupancyActive(std::uint32_t now_ms, std::uint32_t last_motion_ms, std::uint32_t hold_ms, bool motion_seen);
std::uint32_t saturatingEventAdd(std::uint32_t total, std::uint32_t increment);
bool validWifiSsid(const std::string& value);
bool validWifiPassword(const std::string& value);
bool validPosixTimezone(const std::string& value);
bool hardwareParseHttpRequest(const char* data, std::size_t length, std::size_t maximum_body_bytes,
                              char* method, std::size_t method_capacity, char* target, std::size_t target_capacity,
                              std::size_t& content_length, bool& has_content_length);

std::uint32_t hardwareConfigCrc32(const std::uint8_t* data, std::size_t length);
