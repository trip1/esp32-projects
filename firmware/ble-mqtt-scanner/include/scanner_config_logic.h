#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

bool scannerValidWifiSsid(const std::string& value);
bool scannerValidWifiPassword(const std::string& value);
bool scannerValidMqttHost(const std::string& value);
bool scannerValidCredential(const std::string& value, std::size_t maximum_bytes);
bool scannerValidTopicPrefix(const std::string& value);
bool scannerParseHttpRequest(
    const char* data,
    std::size_t length,
    std::size_t maximum_body_bytes,
    char* method,
    std::size_t method_capacity,
    char* target,
    std::size_t target_capacity,
    std::size_t& content_length,
    bool& has_content_length);

std::uint32_t scannerConfigCrc32(const std::uint8_t* data, std::size_t length);
