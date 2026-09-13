#include "diagnostics_logic.h"

#include <cctype>
#include <cstring>

namespace {
bool printable(const std::string& value) {
    for (unsigned char character : value) if (character < 0x20U || character > 0x7eU) return false;
    return true;
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool decode(const char* begin, const char* end, char* output, std::size_t capacity) {
    if (!output || capacity == 0U) return false;
    std::size_t used = 0U;
    while (begin < end) {
        char value = *begin++;
        if (value == '+') value = ' ';
        else if (value == '%') {
            if (end - begin < 2) return false;
            const int high = hexValue(begin[0]);
            const int low = hexValue(begin[1]);
            if (high < 0 || low < 0) return false;
            value = static_cast<char>((high << 4) | low);
            begin += 2;
        }
        if (value == '\0' || used + 1U >= capacity) return false;
        output[used++] = value;
    }
    output[used] = '\0';
    return true;
}

const char* findCrlf(const char* begin, const char* end) {
    for (const char* cursor = begin; cursor + 1 < end; ++cursor) if (cursor[0] == '\r' && cursor[1] == '\n') return cursor;
    return nullptr;
}

bool headerNameEquals(const char* begin, const char* end, const char* expected) {
    if (static_cast<std::size_t>(end - begin) != std::strlen(expected)) return false;
    for (const char* cursor = begin; cursor < end; ++cursor) {
        if (std::tolower(static_cast<unsigned char>(*cursor)) !=
            std::tolower(static_cast<unsigned char>(expected[cursor - begin]))) return false;
    }
    return true;
}

bool copyToken(const char* begin, const char* end, char* output, std::size_t capacity) {
    const std::size_t length = static_cast<std::size_t>(end - begin);
    if (!output || length == 0U || length >= capacity) return false;
    for (const char* cursor = begin; cursor < end; ++cursor) {
        const unsigned char value = static_cast<unsigned char>(*cursor);
        if (value <= 0x20U || value == 0x7fU) return false;
    }
    std::memcpy(output, begin, length);
    output[length] = '\0';
    return true;
}
}  // namespace

bool isValidDiagnosticSsid(const std::string& value) {
    return !value.empty() && value.size() <= 32U && printable(value);
}

bool isValidDiagnosticPassword(const std::string& value) {
    return value.empty() || (value.size() >= 8U && value.size() <= 63U && printable(value));
}

bool parseDiagnosticSetupForm(const char* body, std::size_t length, const char* expected_csrf,
                              char* ssid, std::size_t ssid_capacity,
                              char* password, std::size_t password_capacity) {
    if (!body || !expected_csrf || !ssid || !password || length > 512U) return false;
    bool csrf_seen = false, ssid_seen = false, password_seen = false;
    char csrf[17]{};
    const char* cursor = body;
    const char* end = body + length;
    unsigned fields = 0U;
    while (cursor < end && fields++ < 3U) {
        const char* pair_end = static_cast<const char*>(std::memchr(cursor, '&', static_cast<std::size_t>(end - cursor)));
        if (!pair_end) pair_end = end;
        const char* equals = static_cast<const char*>(std::memchr(cursor, '=', static_cast<std::size_t>(pair_end - cursor)));
        if (!equals) return false;
        const std::string key(cursor, equals);
        bool ok = false;
        if (key == "csrf" && !csrf_seen) { csrf_seen = true; ok = decode(equals + 1, pair_end, csrf, sizeof(csrf)); }
        else if (key == "ssid" && !ssid_seen) { ssid_seen = true; ok = decode(equals + 1, pair_end, ssid, ssid_capacity); }
        else if (key == "password" && !password_seen) { password_seen = true; ok = decode(equals + 1, pair_end, password, password_capacity); }
        else return false;
        if (!ok) return false;
        cursor = pair_end < end ? pair_end + 1 : end;
    }
    return cursor == end && csrf_seen && ssid_seen && password_seen &&
           std::strcmp(csrf, expected_csrf) == 0 && isValidDiagnosticSsid(ssid) && isValidDiagnosticPassword(password);
}

bool parseBoundedHttpRequestForHost(const char* data, std::size_t length, std::size_t maximum_body_bytes,
                                    const char* expected_host,
                                    char* method, std::size_t method_capacity,
                                    char* target, std::size_t target_capacity,
                                    std::size_t& content_length, bool& has_content_length) {
    if (!data || !expected_host || *expected_host == '\0' || length == 0U || std::memchr(data, '\0', length)) return false;
    const std::string host(expected_host);
    const std::string host_with_port = host + ":80";
    const std::string origin = "http://" + host;
    const std::string origin_with_port = origin + ":80";
    const char* begin = data;
    const char* end = data + length;
    const char* line_end = findCrlf(begin, end);
    if (!line_end) return false;
    const char* first_space = static_cast<const char*>(std::memchr(begin, ' ', static_cast<std::size_t>(line_end - begin)));
    const char* second_space = first_space ? static_cast<const char*>(std::memchr(first_space + 1, ' ', static_cast<std::size_t>(line_end - first_space - 1))) : nullptr;
    if (!first_space || !second_space || std::memchr(second_space + 1, ' ', static_cast<std::size_t>(line_end - second_space - 1))) return false;
    if (!copyToken(begin, first_space, method, method_capacity) || !copyToken(first_space + 1, second_space, target, target_capacity)) return false;
    const std::string protocol(second_space + 1, line_end);
    if ((std::strcmp(method, "GET") && std::strcmp(method, "POST")) || target[0] != '/' ||
        (protocol != "HTTP/1.1" && protocol != "HTTP/1.0")) return false;

    bool host_seen = false, origin_seen = false;
    has_content_length = false;
    content_length = 0U;
    const char* cursor = line_end + 2;
    while (cursor < end) {
        line_end = findCrlf(cursor, end);
        if (!line_end) return false;
        if (line_end == cursor) return host_seen && (std::strcmp(method, "POST") != 0 || origin_seen);
        const char* colon = static_cast<const char*>(std::memchr(cursor, ':', static_cast<std::size_t>(line_end - cursor)));
        if (!colon || colon == cursor) return false;
        const char* value = colon + 1;
        while (value < line_end && (*value == ' ' || *value == '\t')) ++value;
        const std::string text(value, line_end);
        if (headerNameEquals(cursor, colon, "Host")) {
            if (host_seen || (text != host && text != host_with_port)) return false;
            host_seen = true;
        } else if (headerNameEquals(cursor, colon, "Origin")) {
            if (origin_seen || (text != origin && text != origin_with_port)) return false;
            origin_seen = true;
        } else if (headerNameEquals(cursor, colon, "Referer")) {
            if (text.rfind(origin + "/", 0U) != 0U && text.rfind(origin_with_port + "/", 0U) != 0U) return false;
        } else if (headerNameEquals(cursor, colon, "Transfer-Encoding")) return false;
        else if (headerNameEquals(cursor, colon, "Content-Length")) {
            if (has_content_length || text.empty()) return false;
            std::size_t parsed = 0U;
            for (unsigned char character : text) {
                if (!std::isdigit(character) || parsed > maximum_body_bytes / 10U) return false;
                parsed = parsed * 10U + static_cast<unsigned>(character - '0');
                if (parsed > maximum_body_bytes) return false;
            }
            content_length = parsed;
            has_content_length = true;
        }
        cursor = line_end + 2;
    }
    return false;
}

bool parseBoundedHttpRequest(const char* data, std::size_t length, std::size_t maximum_body_bytes,
                             char* method, std::size_t method_capacity,
                             char* target, std::size_t target_capacity,
                             std::size_t& content_length, bool& has_content_length) {
    return parseBoundedHttpRequestForHost(data, length, maximum_body_bytes, "192.168.4.1",
                                          method, method_capacity, target, target_capacity,
                                          content_length, has_content_length);
}

bool parseBoundedLanHttpRequest(const char* data, std::size_t length, std::size_t maximum_body_bytes,
                                const char* expected_host,
                                char* method, std::size_t method_capacity,
                                char* target, std::size_t target_capacity,
                                std::size_t& content_length, bool& has_content_length) {
    return parseBoundedHttpRequestForHost(data, length, maximum_body_bytes, expected_host,
                                          method, method_capacity, target, target_capacity,
                                          content_length, has_content_length);
}

std::string knownI2cDeviceName(std::uint8_t address) {
    switch (address) {
        case 0x3c: case 0x3d: return "OLED display";
        case 0x40: return "environment / current sensor";
        case 0x48: return "ADC / temperature sensor";
        case 0x50: return "EEPROM";
        case 0x68: case 0x69: return "RTC / IMU";
        case 0x76: case 0x77: return "BME/BMP environmental sensor";
        default: return {};
    }
}

std::uint32_t diagnosticCrc32(const std::uint8_t* data, std::size_t length) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0U; bit < 8U; ++bit) crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

std::uint64_t extendDiagnosticMillis(std::uint64_t total, std::uint32_t previous, std::uint32_t current) {
    return total + static_cast<std::uint32_t>(current - previous);
}

bool diagnosticScanAllowed(std::uint32_t now, std::uint32_t previous, bool has_previous, std::uint32_t cooldown_ms) {
    return !has_previous || static_cast<std::uint32_t>(now - previous) >= cooldown_ms;
}
