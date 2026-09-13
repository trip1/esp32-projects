#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

bool isValidDiagnosticSsid(const std::string& value);
bool isValidDiagnosticPassword(const std::string& value);
bool parseDiagnosticSetupForm(const char* body, std::size_t length, const char* expected_csrf,
                              char* ssid, std::size_t ssid_capacity,
                              char* password, std::size_t password_capacity);
bool parseBoundedHttpRequest(const char* data, std::size_t length, std::size_t maximum_body_bytes,
                             char* method, std::size_t method_capacity,
                             char* target, std::size_t target_capacity,
                             std::size_t& content_length, bool& has_content_length);
bool parseBoundedLanHttpRequest(const char* data, std::size_t length, std::size_t maximum_body_bytes,
                                const char* expected_host,
                                char* method, std::size_t method_capacity,
                                char* target, std::size_t target_capacity,
                                std::size_t& content_length, bool& has_content_length);
std::string knownI2cDeviceName(std::uint8_t address);
std::uint32_t diagnosticCrc32(const std::uint8_t* data, std::size_t length);
std::uint64_t extendDiagnosticMillis(std::uint64_t total, std::uint32_t previous, std::uint32_t current);
bool diagnosticScanAllowed(std::uint32_t now, std::uint32_t previous, bool has_previous, std::uint32_t cooldown_ms);
