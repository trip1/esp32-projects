#include "sensor_config_logic.h"

#include <cctype>
#include <cmath>
#include <cstring>

namespace {
bool isPrintableAscii(const std::string& value) {
    for (const unsigned char character : value) {
        if (character < 0x20U || character > 0x7eU) return false;
    }
    return true;
}

bool equalHeaderName(const char* begin, const char* end, const char* expected) {
    const size_t length = static_cast<size_t>(end - begin);
    if (length != std::strlen(expected)) return false;
    for (size_t index = 0; index < length; ++index) {
        if (std::tolower(static_cast<unsigned char>(begin[index])) !=
            std::tolower(static_cast<unsigned char>(expected[index]))) return false;
    }
    return true;
}

const char* findCrlf(const char* begin, const char* end) {
    for (const char* cursor = begin; cursor + 1 < end; ++cursor) {
        if (cursor[0] == '\r' && cursor[1] == '\n') return cursor;
    }
    return nullptr;
}

bool validRequestLine(const char* begin, const char* end) {
    const char* first_space = static_cast<const char*>(std::memchr(begin, ' ', end - begin));
    if (first_space == nullptr) return false;
    const char* second_space = static_cast<const char*>(std::memchr(first_space + 1, ' ', end - first_space - 1));
    if (second_space == nullptr || std::memchr(second_space + 1, ' ', end - second_space - 1) != nullptr) return false;
    const std::string method(begin, first_space);
    const std::string protocol(second_space + 1, end);
    return (method == "GET" || method == "POST") && first_space + 1 < second_space && first_space[1] == '/' &&
           (protocol == "HTTP/1.1" || protocol == "HTTP/1.0");
}
}  // namespace

EnvironmentalSensorChip classifyEnvironmentalSensorChip(std::uint8_t chip_id) {
    if (chip_id == 0x60U) return EnvironmentalSensorChip::Bme280;
    if (chip_id >= 0x56U && chip_id <= 0x58U) return EnvironmentalSensorChip::Bmp280;
    if (chip_id == 0x00U || chip_id == 0xffU) return EnvironmentalSensorChip::None;
    return EnvironmentalSensorChip::Other;
}

const char* environmentalSensorChipName(std::uint8_t chip_id) {
    switch (classifyEnvironmentalSensorChip(chip_id)) {
        case EnvironmentalSensorChip::Bme280: return "BME280";
        case EnvironmentalSensorChip::Bmp280: return "BMP280 (no humidity sensor)";
        case EnvironmentalSensorChip::Other: return "other sensor";
        default: return "unknown device";
    }
}

bool isValidWifiSsid(const std::string& value) {
    return !value.empty() && value.size() <= 32U && isPrintableAscii(value);
}

bool isValidWifiPassword(const std::string& value) {
    return value.empty() || (value.size() >= 8U && value.size() <= 63U && isPrintableAscii(value));
}

bool isValidMqttHost(const std::string& value) {
    if (value.empty() || value.size() > 15U) return false;
    std::size_t start = 0U;
    for (unsigned part = 0U; part < 4U; ++part) {
        const std::size_t end = part == 3U ? value.size() : value.find('.', start);
        if (end == std::string::npos || end == start || end - start > 3U) return false;
        unsigned octet = 0U;
        for (std::size_t index = start; index < end; ++index) {
            const unsigned char character = static_cast<unsigned char>(value[index]);
            if (!std::isdigit(character)) return false;
            octet = octet * 10U + static_cast<unsigned>(character - '0');
        }
        if (octet > 255U || (end - start > 1U && value[start] == '0')) return false;
        start = end + 1U;
    }
    return start == value.size() + 1U;
}

bool isValidOptionalCredential(const std::string& value, std::size_t maximum_bytes) {
    return value.size() <= maximum_bytes && isPrintableAscii(value);
}

bool isValidTopicPrefix(const std::string& value) {
    if (value.empty() || value.size() > 96U || value.front() == '/' || value.back() == '/' || value.find("//") != std::string::npos) return false;
    for (const unsigned char character : value) {
        if (!std::isalnum(character) && character != '/' && character != '_' && character != '-' && character != '.') return false;
    }
    return true;
}

bool isValidSleepMinutes(std::uint32_t value) {
    return value >= 1U && value <= 1440U;
}

bool isValidBme280Reading(float temperature_c, float humidity_percent, float pressure_hpa) {
    return std::isfinite(temperature_c) && temperature_c >= -40.0F && temperature_c <= 85.0F &&
           std::isfinite(humidity_percent) && humidity_percent >= 0.0F && humidity_percent <= 100.0F &&
           std::isfinite(pressure_hpa) && pressure_hpa >= 300.0F && pressure_hpa <= 1100.0F;
}

bool mqttDeliveryMatches(
    const char* expected_topic,
    const char* expected_payload,
    std::size_t expected_length,
    const char* received_topic,
    const std::uint8_t* received_payload,
    std::size_t received_length) {
    return expected_topic != nullptr && expected_payload != nullptr && received_topic != nullptr && received_payload != nullptr &&
           expected_length == received_length && std::strcmp(expected_topic, received_topic) == 0 &&
           std::memcmp(expected_payload, received_payload, expected_length) == 0;
}

bool parseBoundedHttpHeaders(
    const char* data,
    std::size_t length,
    std::size_t maximum_body_bytes,
    std::size_t& content_length,
    bool& has_content_length) {
    content_length = 0;
    has_content_length = false;
    if (data == nullptr || length < 4U || std::memchr(data, '\0', length) != nullptr ||
        std::memcmp(data + length - 4U, "\r\n\r\n", 4U) != 0) return false;
    const char* end = data + length;
    const char* request_end = findCrlf(data, end);
    if (request_end == nullptr || !validRequestLine(data, request_end)) return false;
    const char* cursor = request_end + 2;
    while (cursor < end) {
        const char* line_end = findCrlf(cursor, end);
        if (line_end == nullptr) return false;
        if (line_end == cursor) return line_end + 2 == end;
        const char* colon = static_cast<const char*>(std::memchr(cursor, ':', line_end - cursor));
        if (colon == nullptr || colon == cursor) return false;
        for (const char* name = cursor; name < colon; ++name) {
            const unsigned char character = static_cast<unsigned char>(*name);
            if (!std::isalnum(character) && character != '-') return false;
        }
        const char* value_begin = colon + 1;
        while (value_begin < line_end && (*value_begin == ' ' || *value_begin == '\t')) ++value_begin;
        const char* value_end = line_end;
        while (value_end > value_begin && (value_end[-1] == ' ' || value_end[-1] == '\t')) --value_end;
        for (const char* value = value_begin; value < value_end; ++value) {
            const unsigned char character = static_cast<unsigned char>(*value);
            if (character < 0x20U && character != '\t') return false;
        }
        if (equalHeaderName(cursor, colon, "transfer-encoding")) return false;
        if (equalHeaderName(cursor, colon, "content-length")) {
            if (has_content_length || value_begin == value_end) return false;
            size_t parsed = 0;
            for (const char* digit = value_begin; digit < value_end; ++digit) {
                if (*digit < '0' || *digit > '9') return false;
                const size_t next = parsed * 10U + static_cast<size_t>(*digit - '0');
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

bool parseBoundedHttpRequest(
    const char* data,
    std::size_t length,
    std::size_t maximum_body_bytes,
    char* method,
    std::size_t method_capacity,
    char* target,
    std::size_t target_capacity,
    std::size_t& content_length,
    bool& has_content_length) {
    content_length = 0U;
    has_content_length = false;
    if (data == nullptr || method == nullptr || target == nullptr || method_capacity == 0U || target_capacity == 0U ||
        length < 4U || std::memchr(data, '\0', length) != nullptr ||
        std::memcmp(data + length - 4U, "\r\n\r\n", 4U) != 0) return false;
    const char* end = data + length;
    const char* request_end = findCrlf(data, end);
    if (request_end == nullptr || !validRequestLine(data, request_end)) return false;
    const char* first_space = static_cast<const char*>(std::memchr(data, ' ', request_end - data));
    const char* second_space = first_space == nullptr ? nullptr :
        static_cast<const char*>(std::memchr(first_space + 1, ' ', request_end - first_space - 1));
    if (first_space == nullptr || second_space == nullptr) return false;
    const size_t method_length = static_cast<size_t>(first_space - data);
    const size_t target_length = static_cast<size_t>(second_space - first_space - 1);
    if (method_length == 0U || method_length >= method_capacity || target_length == 0U || target_length >= target_capacity) return false;
    for (const char* cursor = first_space + 1; cursor < second_space; ++cursor) {
        const unsigned char character = static_cast<unsigned char>(*cursor);
        if (character <= 0x20U || character == 0x7fU) return false;
    }
    std::memcpy(method, data, method_length); method[method_length] = '\0';
    std::memcpy(target, first_space + 1, target_length); target[target_length] = '\0';
    const bool state_changing = std::strcmp(method, "POST") == 0;
    bool host_seen = false; bool origin_seen = false; bool referer_seen = false;
    const char* cursor = request_end + 2;
    while (cursor < end) {
        const char* line_end = findCrlf(cursor, end);
        if (line_end == nullptr) return false;
        if (line_end == cursor) return line_end + 2 == end && host_seen;
        const char* colon = static_cast<const char*>(std::memchr(cursor, ':', line_end - cursor));
        if (colon == nullptr || colon == cursor) return false;
        for (const char* name = cursor; name < colon; ++name) {
            const unsigned char character = static_cast<unsigned char>(*name);
            if (!std::isalnum(character) && character != '-') return false;
        }
        const char* value_begin = colon + 1;
        while (value_begin < line_end && (*value_begin == ' ' || *value_begin == '\t')) ++value_begin;
        const char* value_end = line_end;
        while (value_end > value_begin && (value_end[-1] == ' ' || value_end[-1] == '\t')) --value_end;
        for (const char* value = value_begin; value < value_end; ++value) {
            const unsigned char character = static_cast<unsigned char>(*value);
            if (character < 0x20U && character != '\t') return false;
        }
        if (equalHeaderName(cursor, colon, "transfer-encoding")) return false;
        if (equalHeaderName(cursor, colon, "host")) {
            const std::string value(value_begin, value_end);
            if (host_seen || (value != "192.168.4.1" && value != "192.168.4.1:80")) return false;
            host_seen = true;
        } else if (equalHeaderName(cursor, colon, "origin")) {
            const std::string value(value_begin, value_end);
            if (origin_seen || (state_changing && value != "http://192.168.4.1")) return false;
            origin_seen = true;
        } else if (equalHeaderName(cursor, colon, "referer")) {
            const std::string value(value_begin, value_end);
            if (referer_seen || (state_changing && value.rfind("http://192.168.4.1/", 0U) != 0U)) return false;
            referer_seen = true;
        } else if (equalHeaderName(cursor, colon, "content-length")) {
            if (has_content_length || value_begin == value_end) return false;
            size_t parsed = 0U;
            for (const char* digit = value_begin; digit < value_end; ++digit) {
                if (*digit < '0' || *digit > '9') return false;
                const size_t next = parsed * 10U + static_cast<size_t>(*digit - '0');
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

bool shouldProcessPending(bool pending_record_valid, bool rejected_pending_retained) {
    return pending_record_valid && !rejected_pending_retained;
}

std::uint32_t sleepMinutesAfterPendingAttempt(
    bool pending,
    bool published,
    bool promoted,
    bool active_ready,
    std::uint32_t candidate_minutes,
    std::uint32_t active_minutes,
    std::uint32_t fallback_minutes) {
    if (!pending || (published && promoted)) return candidate_minutes;
    return active_ready ? active_minutes : fallback_minutes;
}

std::uint32_t configurationCrc32(const std::uint8_t* data, std::size_t length) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}
