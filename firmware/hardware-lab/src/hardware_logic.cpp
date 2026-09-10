#include "hardware_logic.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>


namespace {
bool printable(const std::string& value) {
    for (const unsigned char character : value) if (character < 0x20U || character > 0x7eU) return false;
    return true;
}

const char* findCrlf(const char* begin, const char* end) {
    for (const char* cursor = begin; cursor + 1 < end; ++cursor) if (cursor[0] == '\r' && cursor[1] == '\n') return cursor;
    return nullptr;
}

bool equalName(const char* begin, const char* end, const char* expected) {
    const std::size_t length = static_cast<std::size_t>(end - begin);
    if (length != std::strlen(expected)) return false;
    for (std::size_t index = 0U; index < length; ++index) {
        if (std::tolower(static_cast<unsigned char>(begin[index])) != std::tolower(static_cast<unsigned char>(expected[index]))) return false;
    }
    return true;
}
}  // namespace

float distanceCentimetersFromEcho(std::uint32_t echo_microseconds) {
    if (echo_microseconds == 0U || echo_microseconds > 30000U) return NAN;
    return static_cast<float>(echo_microseconds) * 0.0343F * 0.5F;
}

float medianDistance(const float* values, std::size_t count) {
    if (values == nullptr || count == 0U || count > 5U) return NAN;
    std::array<float, 5> finite{};
    std::size_t finite_count = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        if (std::isfinite(values[index]) && values[index] >= 0.0F) finite[finite_count++] = values[index];
    }
    if (finite_count == 0U) return NAN;
    std::sort(finite.begin(), finite.begin() + finite_count);
    const std::size_t middle = finite_count / 2U;
    return finite_count % 2U == 0U ? (finite[middle - 1U] + finite[middle]) * 0.5F : finite[middle];
}

bool occupancyActive(std::uint32_t now_ms, std::uint32_t last_motion_ms, std::uint32_t hold_ms, bool motion_seen) {
    return motion_seen && static_cast<std::uint32_t>(now_ms - last_motion_ms) <= hold_ms;
}

std::uint32_t saturatingEventAdd(std::uint32_t total, std::uint32_t increment) {
    return increment > 0xffffffffU - total ? 0xffffffffU : total + increment;
}

bool validWifiSsid(const std::string& value) { return !value.empty() && value.size() <= 32U && printable(value); }
bool validWifiPassword(const std::string& value) { return value.empty() || (value.size() >= 8U && value.size() <= 63U && printable(value)); }

bool validPosixTimezone(const std::string& value) {
    static constexpr const char* supported[] = {
        "UTC0", "EST5EDT,M3.2.0,M11.1.0", "CST6CDT,M3.2.0,M11.1.0",
        "MST7MDT,M3.2.0,M11.1.0", "MST7", "PST8PDT,M3.2.0,M11.1.0",
        "AKST9AKDT,M3.2.0,M11.1.0", "HST10", "GMT0BST,M3.5.0/1,M10.5.0",
        "CET-1CEST,M3.5.0,M10.5.0/3", "AEST-10AEDT,M10.1.0,M4.1.0/3",
        "JST-9", "IST-5:30",
    };
    for (const char* timezone : supported) if (value == timezone) return true;
    return false;
}

bool hardwareParseHttpRequest(const char* data, std::size_t length, std::size_t maximum_body_bytes,
                              char* method, std::size_t method_capacity, char* target, std::size_t target_capacity,
                              std::size_t& content_length, bool& has_content_length) {
    content_length = 0U;
    has_content_length = false;
    if (data == nullptr || method == nullptr || target == nullptr || method_capacity == 0U || target_capacity == 0U || length < 4U ||
        std::memchr(data, '\0', length) != nullptr || std::memcmp(data + length - 4U, "\r\n\r\n", 4U) != 0) return false;
    const char* end = data + length;
    const char* request_end = findCrlf(data, end);
    if (request_end == nullptr) return false;
    const char* first_space = static_cast<const char*>(std::memchr(data, ' ', request_end - data));
    const char* second_space = first_space == nullptr ? nullptr : static_cast<const char*>(std::memchr(first_space + 1, ' ', request_end - first_space - 1));
    if (first_space == nullptr || second_space == nullptr || std::memchr(second_space + 1, ' ', request_end - second_space - 1) != nullptr) return false;
    const std::size_t method_length = static_cast<std::size_t>(first_space - data);
    const std::size_t target_length = static_cast<std::size_t>(second_space - first_space - 1);
    const std::string protocol(second_space + 1, request_end);
    if (method_length == 0U || method_length >= method_capacity || target_length == 0U || target_length >= target_capacity ||
        (protocol != "HTTP/1.1" && protocol != "HTTP/1.0") || first_space[1] != '/') return false;
    for (const char* cursor = first_space + 1; cursor < second_space; ++cursor) {
        const unsigned char character = static_cast<unsigned char>(*cursor);
        if (character <= 0x20U || character == 0x7fU) return false;
    }
    std::memcpy(method, data, method_length); method[method_length] = '\0';
    std::memcpy(target, first_space + 1, target_length); target[target_length] = '\0';
    if (std::strcmp(method, "GET") != 0 && std::strcmp(method, "POST") != 0) return false;
    const bool state_changing = std::strcmp(method, "POST") == 0;
    bool host_seen = false; bool origin_seen = false; bool referer_seen = false;
    const char* cursor = request_end + 2;
    while (cursor < end) {
        const char* line_end = findCrlf(cursor, end);
        if (line_end == nullptr) return false;
        if (line_end == cursor) return line_end + 2 == end && host_seen;
        const char* colon = static_cast<const char*>(std::memchr(cursor, ':', line_end - cursor));
        if (colon == nullptr || colon == cursor) return false;
        for (const char* name = cursor; name < colon; ++name) if (!std::isalnum(static_cast<unsigned char>(*name)) && *name != '-') return false;
        const char* value_begin = colon + 1;
        while (value_begin < line_end && (*value_begin == ' ' || *value_begin == '\t')) ++value_begin;
        const char* value_end = line_end;
        while (value_end > value_begin && (value_end[-1] == ' ' || value_end[-1] == '\t')) --value_end;
        for (const char* value = value_begin; value < value_end; ++value) if (static_cast<unsigned char>(*value) < 0x20U && *value != '\t') return false;
        if (equalName(cursor, colon, "transfer-encoding")) return false;
        if (equalName(cursor, colon, "host")) {
            const std::string value(value_begin, value_end);
            if (host_seen || (value != "192.168.4.1" && value != "192.168.4.1:80")) return false;
            host_seen = true;
        } else if (equalName(cursor, colon, "origin")) {
            const std::string value(value_begin, value_end);
            if (origin_seen || (state_changing && value != "http://192.168.4.1")) return false;
            origin_seen = true;
        } else if (equalName(cursor, colon, "referer")) {
            const std::string value(value_begin, value_end);
            if (referer_seen || (state_changing && value.rfind("http://192.168.4.1/", 0U) != 0U)) return false;
            referer_seen = true;
        } else if (equalName(cursor, colon, "content-length")) {
            if (has_content_length || value_begin == value_end) return false;
            std::size_t parsed = 0U;
            for (const char* digit = value_begin; digit < value_end; ++digit) {
                if (*digit < '0' || *digit > '9') return false;
                const std::size_t next = parsed * 10U + static_cast<std::size_t>(*digit - '0');
                if (next < parsed || next > maximum_body_bytes) return false;
                parsed = next;
            }
            content_length = parsed;
            has_content_length = true;
        }
        cursor = line_end + 2;
    }
    return false;
}

std::uint32_t hardwareConfigCrc32(const std::uint8_t* data, std::size_t length) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0U; bit < 8U; ++bit) crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
