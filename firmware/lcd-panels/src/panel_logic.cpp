#include "panel_logic.h"

#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
void fitLine(const char* value, char output[17]) {
    std::size_t index = 0U;
    if (value != nullptr) {
        for (; index < 16U && value[index] != '\0'; ++index) {
            const unsigned char character = static_cast<unsigned char>(value[index]);
            output[index] = character >= 0x20U && character <= 0x7eU ? static_cast<char>(character) : ' ';
        }
    }
    while (index < 16U) output[index++] = ' ';
    output[16] = '\0';
}

bool validDecimal(const char* value, double minimum, double maximum) {
    if (value == nullptr || *value == '\0' || std::strlen(value) > 16U) return false;
    char* end = nullptr;
    const double parsed = std::strtod(value, &end);
    return end != value && *end == '\0' && std::isfinite(parsed) && parsed >= minimum && parsed <= maximum;
}

bool terminated(const char* value, std::size_t capacity) {
    return std::memchr(value, '\0', capacity) != nullptr;
}

bool printableBounded(const char* value, std::size_t capacity, bool allow_empty) {
    if (!terminated(value, capacity)) return false;
    const std::size_t length = std::strlen(value);
    if (!allow_empty && length == 0U) return false;
    for (std::size_t index = 0U; index < length; ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character < 0x20U || character > 0x7eU) return false;
    }
    return true;
}

bool privateIpv4(const char* begin, const char* end) {
    unsigned octets[4]{};
    for (unsigned part = 0U; part < 4U; ++part) {
        if (begin >= end || *begin < '0' || *begin > '9') return false;
        const char* part_begin = begin;
        unsigned value = 0U;
        unsigned digits = 0U;
        while (begin < end && *begin >= '0' && *begin <= '9') {
            value = value * 10U + static_cast<unsigned>(*begin++ - '0');
            if (++digits > 3U || value > 255U) return false;
        }
        octets[part] = value;
        if (digits > 1U && *part_begin == '0') return false;
        if (part < 3U) {
            if (begin >= end || *begin++ != '.') return false;
        } else if (begin != end) return false;
    }
    return octets[0] == 10U || (octets[0] == 172U && octets[1] >= 16U && octets[1] <= 31U)
        || (octets[0] == 192U && octets[1] == 168U);
}

bool validUnifiUrl(const char* value) {
    if (!printableBounded(value, 161U, false) || std::strchr(value, '@') != nullptr || std::strchr(value, '#') != nullptr || std::strchr(value, '?') != nullptr) return false;
    for (const char* cursor = value; *cursor != '\0'; ++cursor) if (std::isspace(static_cast<unsigned char>(*cursor))) return false;
    if (std::strncmp(value, "http://", 7U) != 0) return false;
    const char* authority = value + 7U;
    const char* path = std::strchr(authority, '/');
    const char* authority_end = path == nullptr ? value + std::strlen(value) : path;
    if (authority == authority_end || path == nullptr || std::strcmp(path, "/api/unifi/summary") != 0) return false;
    const char* colon = static_cast<const char*>(std::memchr(authority, ':', static_cast<std::size_t>(authority_end - authority)));
    const char* host_end = colon == nullptr ? authority_end : colon;
    unsigned port = 80U;
    if (colon != nullptr) {
        if (++colon == authority_end) return false;
        if (*colon == '0' && colon + 1 != authority_end) return false;
        port = 0U;
        for (const char* digit = colon; digit < authority_end; ++digit) {
            if (*digit < '0' || *digit > '9') return false;
            port = port * 10U + static_cast<unsigned>(*digit - '0');
            if (port > 65535U) return false;
        }
        if (port == 0U) return false;
    }
    return (port == 80U || port == 8080U || port == 8090U) && privateIpv4(authority, host_end);
}

const char* findCrlf(const char* begin, const char* end) {
    for (const char* cursor = begin; cursor + 1 < end; ++cursor) {
        if (cursor[0] == '\r' && cursor[1] == '\n') return cursor;
    }
    return nullptr;
}

bool equalHeaderName(const char* begin, const char* end, const char* expected) {
    const std::size_t length = static_cast<std::size_t>(end - begin);
    if (length != std::strlen(expected)) return false;
    for (std::size_t index = 0U; index < length; ++index) {
        if (std::tolower(static_cast<unsigned char>(begin[index])) != std::tolower(static_cast<unsigned char>(expected[index]))) return false;
    }
    return true;
}
}

void formatMqttText(const char* label, const char* value, char top[17], char bottom[17]) {
    fitLine(label, top);
    fitLine(value, bottom);
}

void formatWeather(float temperature_c, int humidity_percent, const char* condition, char top[17], char bottom[17]) {
    if (!std::isfinite(temperature_c) || humidity_percent < 0 || humidity_percent > 100 || condition == nullptr) {
        fitLine("Weather offline", top);
        fitLine("No current data", bottom);
        return;
    }
    char text[48]{};
    std::snprintf(text, sizeof(text), "Weather %.1f C", static_cast<double>(temperature_c));
    fitLine(text, top);
    std::snprintf(text, sizeof(text), "Hum %d%% %s", humidity_percent, condition);
    fitLine(text, bottom);
}

void formatUnifi(bool wan_up, int latency_ms, int clients, int access_points, char top[17], char bottom[17]) {
    char text[48]{};
    if (wan_up && latency_ms >= 0) std::snprintf(text, sizeof(text), "WAN UP    %d ms", latency_ms);
    else std::snprintf(text, sizeof(text), "WAN %s", wan_up ? "UP" : "DOWN");
    fitLine(text, top);
    if (clients >= 0 && access_points >= 0) std::snprintf(text, sizeof(text), "%d clients %d AP", clients, access_points);
    else std::snprintf(text, sizeof(text), "Stats unavail.");
    fitLine(text, bottom);
}

void formatSatellite(double latitude, double longitude, int altitude_km, char top[17], char bottom[17]) {
    if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0 || altitude_km < 0) {
        fitLine("ISS unavailable", top);
        fitLine("Position unknown", bottom);
        return;
    }
    char text[48]{};
    std::snprintf(text, sizeof(text), "ISS %.2f %c", std::fabs(latitude), latitude < 0.0 ? 'S' : 'N');
    fitLine(text, top);
    std::snprintf(text, sizeof(text), "%.2f %c %d km", std::fabs(longitude), longitude < 0.0 ? 'W' : 'E', altitude_km);
    fitLine(text, bottom);
}

bool validExactMqttTopic(const char* value) {
    if (value == nullptr) return false;
    const std::size_t length = std::strlen(value);
    if (length == 0U || length > 128U || value[0] == '/' || value[length - 1U] == '/') return false;
    for (std::size_t index = 0U; index < length; ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character < 0x20U || character > 0x7eU || character == '+' || character == '#') return false;
    }
    return true;
}

bool validLatitude(const char* value) { return validDecimal(value, -90.0, 90.0); }
bool validLongitude(const char* value) { return validDecimal(value, -180.0, 180.0); }

bool mqttMessageStale(std::uint32_t now, std::uint32_t last_message_ms, bool message_received) {
    return message_received && static_cast<std::uint32_t>(now - last_message_ms) > 120000U;
}

bool buildWeatherPath(double latitude, double longitude, char* output, std::size_t capacity) {
    if (output == nullptr || capacity == 0U || !std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0) return false;
    const int written = std::snprintf(output, capacity, "/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,weather_code&temperature_unit=celsius&timeformat=unixtime", latitude, longitude);
    return written > 0 && static_cast<std::size_t>(written) < capacity;
}

const char* weatherCondition(int code) {
    if (code == 0) return "Clear";
    if (code >= 1 && code <= 3) return "Cloudy";
    if (code == 45 || code == 48) return "Fog";
    if ((code >= 51 && code <= 57)) return "Drizzle";
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "Rain";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "Snow";
    if (code == 95 || code == 96 || code == 99) return "Storm";
    return "Unknown";
}

bool panelConfigValid(PanelKind kind, const PanelConfig& value) {
    if (!printableBounded(value.wifi_ssid, sizeof(value.wifi_ssid), false)
        || !printableBounded(value.wifi_password, sizeof(value.wifi_password), true)) return false;
    const std::size_t password_length = std::strlen(value.wifi_password);
    if (password_length != 0U && password_length < 8U) return false;
    if (kind == PanelKind::Space) return true;
    if (kind == PanelKind::Weather) return validLatitude(value.latitude) && validLongitude(value.longitude);
    if (kind == PanelKind::Unifi) return validUnifiUrl(value.unifi_url);
    if (kind != PanelKind::Mqtt || !printableBounded(value.mqtt_host, sizeof(value.mqtt_host), false)
        || !privateIpv4(value.mqtt_host, value.mqtt_host + std::strlen(value.mqtt_host)) || value.mqtt_port == 0U
        || !validExactMqttTopic(value.mqtt_topic) || !printableBounded(value.label, sizeof(value.label), false)
        || !printableBounded(value.mqtt_username, sizeof(value.mqtt_username), true)
        || !printableBounded(value.mqtt_password, sizeof(value.mqtt_password), true)) return false;
    return std::strlen(value.mqtt_username) != 0U || std::strlen(value.mqtt_password) == 0U;
}

std::uint32_t panelCrc32(const unsigned char* data, std::size_t length) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0U; bit < 8U; ++bit) crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool panelParseHttpRequest(const char* data, std::size_t length, std::size_t maximum_body_bytes,
                           char* method, std::size_t method_capacity, char* target, std::size_t target_capacity,
                           std::size_t& content_length, bool& has_content_length) {
    content_length = 0U;
    has_content_length = false;
    if (data == nullptr || method == nullptr || target == nullptr || method_capacity == 0U || target_capacity == 0U || length < 4U
        || std::memchr(data, '\0', length) != nullptr || std::memcmp(data + length - 4U, "\r\n\r\n", 4U) != 0) return false;
    const char* end = data + length;
    const char* request_end = findCrlf(data, end);
    if (request_end == nullptr) return false;
    const char* first_space = static_cast<const char*>(std::memchr(data, ' ', static_cast<std::size_t>(request_end - data)));
    const char* second_space = first_space == nullptr ? nullptr : static_cast<const char*>(std::memchr(first_space + 1, ' ', static_cast<std::size_t>(request_end - first_space - 1)));
    if (first_space == nullptr || second_space == nullptr || std::memchr(second_space + 1, ' ', static_cast<std::size_t>(request_end - second_space - 1)) != nullptr) return false;
    const std::size_t method_length = static_cast<std::size_t>(first_space - data);
    const std::size_t target_length = static_cast<std::size_t>(second_space - first_space - 1);
    const std::string protocol(second_space + 1, request_end);
    if (method_length == 0U || method_length >= method_capacity || target_length == 0U || target_length >= target_capacity
        || (protocol != "HTTP/1.1" && protocol != "HTTP/1.0") || first_space[1] != '/') return false;
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
        if (line_end == cursor) return line_end + 2 == end && host_seen && (!state_changing || origin_seen);
        const char* colon = static_cast<const char*>(std::memchr(cursor, ':', static_cast<std::size_t>(line_end - cursor)));
        if (colon == nullptr || colon == cursor) return false;
        for (const char* name = cursor; name < colon; ++name) if (!std::isalnum(static_cast<unsigned char>(*name)) && *name != '-') return false;
        const char* value_begin = colon + 1;
        while (value_begin < line_end && (*value_begin == ' ' || *value_begin == '\t')) ++value_begin;
        const char* value_end = line_end;
        while (value_end > value_begin && (value_end[-1] == ' ' || value_end[-1] == '\t')) --value_end;
        for (const char* value = value_begin; value < value_end; ++value) if (static_cast<unsigned char>(*value) < 0x20U && *value != '\t') return false;
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
