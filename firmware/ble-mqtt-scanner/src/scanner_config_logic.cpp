#include "scanner_config_logic.h"

#include <cctype>
#include <cstring>

namespace {
bool printableAscii(const std::string& value) {
    for (const unsigned char character : value) {
        if (character < 0x20U || character > 0x7eU) return false;
    }
    return true;
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
    for (std::size_t index = 0; index < length; ++index) {
        if (std::tolower(static_cast<unsigned char>(begin[index])) !=
            std::tolower(static_cast<unsigned char>(expected[index]))) return false;
    }
    return true;
}

bool parseRequestLine(const char* begin, const char* end, char* method, std::size_t method_capacity,
                      char* target, std::size_t target_capacity) {
    const char* first_space = static_cast<const char*>(std::memchr(begin, ' ', end - begin));
    if (first_space == nullptr) return false;
    const char* second_space = static_cast<const char*>(std::memchr(first_space + 1, ' ', end - first_space - 1));
    if (second_space == nullptr || std::memchr(second_space + 1, ' ', end - second_space - 1) != nullptr) return false;
    const std::size_t method_length = static_cast<std::size_t>(first_space - begin);
    const std::size_t target_length = static_cast<std::size_t>(second_space - first_space - 1);
    if (method_length == 0U || method_length >= method_capacity || target_length == 0U || target_length >= target_capacity) return false;
    for (const char* cursor = first_space + 1; cursor < second_space; ++cursor) {
        const unsigned char character = static_cast<unsigned char>(*cursor);
        if (character <= 0x20U || character == 0x7fU) return false;
    }
    const std::string protocol(second_space + 1, end);
    if ((method_length != 3U && method_length != 4U) ||
        (std::memcmp(begin, "GET", method_length) != 0 && std::memcmp(begin, "POST", method_length) != 0) ||
        (protocol != "HTTP/1.1" && protocol != "HTTP/1.0") || first_space[1] != '/') return false;
    std::memcpy(method, begin, method_length);
    method[method_length] = '\0';
    std::memcpy(target, first_space + 1, target_length);
    target[target_length] = '\0';
    return true;
}
}  // namespace

bool scannerValidWifiSsid(const std::string& value) {
    return !value.empty() && value.size() <= 32U && printableAscii(value);
}

bool scannerValidWifiPassword(const std::string& value) {
    return value.empty() || (value.size() >= 8U && value.size() <= 63U && printableAscii(value));
}

bool scannerValidMqttHost(const std::string& value) {
    if (value.empty() || value.size() > 128U) return false;
    for (const unsigned char character : value) {
        if (!std::isalnum(character) && character != '.' && character != '-' && character != '_' && character != ':') return false;
    }
    return true;
}

bool scannerValidCredential(const std::string& value, std::size_t maximum_bytes) {
    return value.size() <= maximum_bytes && printableAscii(value);
}

bool scannerValidTopicPrefix(const std::string& value) {
    if (value.empty() || value.size() > 96U || value.front() == '/' || value.back() == '/' || value.find("//") != std::string::npos) return false;
    for (const unsigned char character : value) {
        if (!std::isalnum(character) && character != '/' && character != '_' && character != '-' && character != '.') return false;
    }
    return true;
}

bool scannerParseHttpRequest(const char* data, std::size_t length, std::size_t maximum_body_bytes,
                             char* method, std::size_t method_capacity, char* target, std::size_t target_capacity,
                             std::size_t& content_length, bool& has_content_length) {
    content_length = 0;
    has_content_length = false;
    if (data == nullptr || method == nullptr || target == nullptr || method_capacity == 0U || target_capacity == 0U ||
        length < 4U || std::memchr(data, '\0', length) != nullptr ||
        std::memcmp(data + length - 4U, "\r\n\r\n", 4U) != 0) return false;
    const char* end = data + length;
    const char* request_end = findCrlf(data, end);
    if (request_end == nullptr || !parseRequestLine(data, request_end, method, method_capacity, target, target_capacity)) return false;
    const bool state_changing = std::strcmp(method, "POST") == 0;
    bool host_seen = false;
    bool origin_seen = false;
    bool referer_seen = false;
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
            std::size_t parsed = 0;
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

std::uint32_t scannerConfigCrc32(const std::uint8_t* data, std::size_t length) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8U; ++bit) crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
