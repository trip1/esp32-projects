#include "bounded_http.h"
#include "bounded_http_logic.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
struct ParsedUrl {
    bool secure = false;
    char host[129]{};
    uint16_t port = 0U;
    char path[257]{};
};

bool parseUrl(const char* url, ParsedUrl& parsed) {
    if (url == nullptr) return false;
    const char* authority = nullptr;
    if (std::strncmp(url, "https://", 8U) == 0) { parsed.secure = true; parsed.port = 443U; authority = url + 8U; }
    else if (std::strncmp(url, "http://", 7U) == 0) { parsed.secure = false; parsed.port = 80U; authority = url + 7U; }
    else return false;
    const char* path = std::strchr(authority, '/');
    if (path == nullptr || path == authority || std::strlen(path) >= sizeof(parsed.path)) return false;
    const char* colon = static_cast<const char*>(std::memchr(authority, ':', static_cast<size_t>(path - authority)));
    const char* host_end = colon == nullptr ? path : colon;
    const size_t host_length = static_cast<size_t>(host_end - authority);
    if (host_length == 0U || host_length >= sizeof(parsed.host)) return false;
    std::memcpy(parsed.host, authority, host_length); parsed.host[host_length] = '\0';
    if (colon != nullptr) {
        unsigned port = 0U;
        for (const char* cursor = colon + 1; cursor < path; ++cursor) {
            if (*cursor < '0' || *cursor > '9') return false;
            port = port * 10U + static_cast<unsigned>(*cursor - '0');
            if (port > 65535U) return false;
        }
        if (port == 0U) return false;
        parsed.port = static_cast<uint16_t>(port);
    }
    std::strcpy(parsed.path, path);
    return true;
}

bool readByte(Client& client, uint8_t& output, uint32_t started, uint32_t timeout_ms) {
    while (static_cast<uint32_t>(millis() - started) < timeout_ms) {
        if (client.available()) {
            const int value = client.read();
            if (value >= 0) { output = static_cast<uint8_t>(value); return true; }
        } else if (!client.connected()) return false;
        delay(1);
    }
    return false;
}

bool readLine(Client& client, char* output, size_t capacity, size_t& total_header, uint32_t started, uint32_t timeout_ms) {
    size_t used = 0U;
    while (used + 1U < capacity) {
        uint8_t value = 0U;
        if (!readByte(client, value, started, timeout_ms)) return false;
        if (++total_header > 2048U || value == '\0' || value == '\n') return false;
        if (value == '\r') {
            uint8_t newline = 0U;
            if (!readByte(client, newline, started, timeout_ms) || ++total_header > 2048U || newline != '\n') return false;
            output[used] = '\0';
            return true;
        }
        if (value < 0x20U && value != '\t') return false;
        output[used++] = static_cast<char>(value);
    }
    return false;
}

bool readHeaders(Client& client, char* output, size_t capacity, size_t& length, uint32_t started) {
    length = 0U;
    while (length + 1U < capacity) {
        uint8_t value = 0U;
        if (!readByte(client, value, started, 7000U) || value == '\0') return false;
        output[length++] = static_cast<char>(value);
        output[length] = '\0';
        if (length >= 4U && std::memcmp(output + length - 4U, "\r\n\r\n", 4U) == 0) return true;
    }
    return false;
}


bool readContentLength(Client& client, char* output, size_t capacity, size_t length, uint32_t started) {
    for (size_t index = 0U; index < length; ++index) {
        uint8_t value = 0U;
        if (!readByte(client, value, started, 7000U)) return false;
        output[index] = static_cast<char>(value);
    }
    output[length] = '\0';
    return true;
}

bool readChunked(Client& client, char* output, size_t capacity, uint32_t started) {
    size_t written = 0U;
    size_t framing = 0U;
    while (true) {
        char line[24]{};
        if (!readLine(client, line, sizeof(line), framing, started, 7000U) || line[0] == '\0' || std::strchr(line, ';') != nullptr) return false;
        size_t chunk = 0U;
        for (const char* cursor = line; *cursor != '\0'; ++cursor) {
            int value = -1;
            if (*cursor >= '0' && *cursor <= '9') value = *cursor - '0';
            else if (*cursor >= 'a' && *cursor <= 'f') value = *cursor - 'a' + 10;
            else if (*cursor >= 'A' && *cursor <= 'F') value = *cursor - 'A' + 10;
            const size_t maximum = capacity - 1U - written;
            if (value < 0 || static_cast<size_t>(value) > maximum
                || chunk > (maximum - static_cast<size_t>(value)) / 16U) return false;
            chunk = chunk * 16U + static_cast<size_t>(value);
        }
        if (chunk == 0U) {
            if (!readLine(client, line, sizeof(line), framing, started, 7000U) || line[0] != '\0') return false;
            output[written] = '\0';
            return written != 0U;
        }
        if (chunk > capacity - 1U - written) return false;
        for (size_t index = 0U; index < chunk; ++index) {
            uint8_t value = 0U;
            if (!readByte(client, value, started, 7000U)) return false;
            output[written++] = static_cast<char>(value);
        }
        uint8_t cr = 0U; uint8_t lf = 0U;
        if (!readByte(client, cr, started, 7000U) || !readByte(client, lf, started, 7000U) || cr != '\r' || lf != '\n') return false;
    }
}

bool exchange(Client& client, const ParsedUrl& parsed, const char* coordinate_headers, char* output, size_t capacity) {
    char request[512]{};
    char host_header[160]{};
    const bool default_port = (parsed.secure && parsed.port == 443U) || (!parsed.secure && parsed.port == 80U);
    const int host_length = default_port ? std::snprintf(host_header, sizeof(host_header), "%s", parsed.host)
                                         : std::snprintf(host_header, sizeof(host_header), "%s:%u", parsed.host, parsed.port);
    if (host_length <= 0 || static_cast<size_t>(host_length) >= sizeof(host_header)) return false;
    const int request_length = std::snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\nHost: %s\r\nAccept: application/json\r\n%sConnection: close\r\n\r\n", parsed.path, host_header, coordinate_headers == nullptr ? "" : coordinate_headers);
    if (request_length <= 0 || static_cast<size_t>(request_length) >= sizeof(request)
        || client.write(reinterpret_cast<const uint8_t*>(request), static_cast<size_t>(request_length)) != static_cast<size_t>(request_length)) return false;
    const uint32_t started = millis();
    char response_headers[2049]{};
    size_t header_bytes = 0U;
    HttpResponseFraming framing{};
    if (!readHeaders(client, response_headers, sizeof(response_headers), header_bytes, started)
        || !panelParseResponseHeaders(response_headers, header_bytes, sizeof(response_headers) - 1U, capacity - 1U, framing)) return false;
    return framing.chunked ? readChunked(client, output, capacity, started)
                           : readContentLength(client, output, capacity, framing.content_length, started);
}

bool get(const char* url, const char* ca_certificate, const char* coordinate_headers, char* output, std::size_t capacity) {
    ParsedUrl parsed{};
    if (output == nullptr || capacity < 2U || capacity > 2049U || !parseUrl(url, parsed)) return false;
    bool result = false;
    if (parsed.secure) {
        if (ca_certificate == nullptr) return false;
        WiFiClientSecure client;
        client.setCACert(ca_certificate);
        client.setHandshakeTimeout(3U);
        client.setTimeout(3000U);
        if (client.connect(parsed.host, parsed.port, 3000)) result = exchange(client, parsed, coordinate_headers, output, capacity);
        client.stop();
    } else {
        IPAddress address;
        if (!address.fromString(parsed.host)) return false;
        WiFiClient client;
        client.setTimeout(3000U);
        if (client.connect(address, parsed.port, 3000)) result = exchange(client, parsed, coordinate_headers, output, capacity);
        client.stop();
    }
    return result;
}
}

bool panelHttpGetBounded(const char* url, const char* ca_certificate, char* output, std::size_t capacity) {
    return get(url, ca_certificate, nullptr, output, capacity);
}

bool panelHttpGetBoundedAt(const char* url, const char* ca_certificate, double latitude, double longitude, char* output, std::size_t capacity) {
    if (!std::isfinite(latitude) || latitude < -90.0 || latitude > 90.0 || !std::isfinite(longitude) || longitude < -180.0 || longitude > 180.0) return false;
    char headers[96]{};
    const int length = std::snprintf(headers, sizeof(headers), "X-LCD-Latitude: %.1f\r\nX-LCD-Longitude: %.1f\r\n", latitude, longitude);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(headers)) return false;
    return get(url, ca_certificate, headers, output, capacity);
}
