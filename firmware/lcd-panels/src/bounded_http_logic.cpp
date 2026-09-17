#include "bounded_http_logic.h"

#include <cctype>
#include <cstring>
#include <limits>

namespace {
const char* findCrlf(const char* begin, const char* end) {
    for (const char* cursor = begin; cursor + 1 < end; ++cursor) {
        if (cursor[0] == '\r' && cursor[1] == '\n') return cursor;
    }
    return nullptr;
}

bool nameEquals(const char* begin, const char* end, const char* expected) {
    const std::size_t length = static_cast<std::size_t>(end - begin);
    if (length != std::strlen(expected)) return false;
    for (std::size_t index = 0U; index < length; ++index) {
        if (std::tolower(static_cast<unsigned char>(begin[index]))
            != std::tolower(static_cast<unsigned char>(expected[index]))) return false;
    }
    return true;
}

void trim(const char*& begin, const char*& end) {
    while (begin < end && (*begin == ' ' || *begin == '\t')) ++begin;
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) --end;
}
}

bool panelParseResponseHeaders(const char* data, std::size_t length, std::size_t maximum_header_bytes,
                               std::size_t maximum_body_bytes, HttpResponseFraming& framing) {
    framing = {};
    if (data == nullptr || length < 4U || length > maximum_header_bytes
        || std::memchr(data, '\0', length) != nullptr
        || std::memcmp(data + length - 4U, "\r\n\r\n", 4U) != 0) return false;
    const char* end = data + length;
    const char* line_end = findCrlf(data, end);
    if (line_end == nullptr
        || (!nameEquals(data, line_end, "HTTP/1.1 200 OK") && !nameEquals(data, line_end, "HTTP/1.0 200 OK"))) return false;

    bool transfer_encoding_seen = false;
    const char* cursor = line_end + 2;
    while (cursor < end - 2) {
        line_end = findCrlf(cursor, end);
        if (line_end == nullptr || line_end == cursor) return false;
        const char* colon = static_cast<const char*>(std::memchr(cursor, ':', static_cast<std::size_t>(line_end - cursor)));
        if (colon == nullptr || colon == cursor) return false;
        for (const char* name = cursor; name < colon; ++name) {
            if (!std::isalnum(static_cast<unsigned char>(*name)) && *name != '-') return false;
        }
        const char* value_begin = colon + 1;
        const char* value_end = line_end;
        trim(value_begin, value_end);
        if (value_begin == value_end) return false;
        for (const char* value = value_begin; value < value_end; ++value) {
            const unsigned char character = static_cast<unsigned char>(*value);
            if ((character < 0x20U && character != '\t') || character == 0x7fU) return false;
        }
        if (nameEquals(cursor, colon, "content-length")) {
            if (framing.has_content_length) return false;
            framing.has_content_length = true;
            std::size_t parsed = 0U;
            for (const char* digit = value_begin; digit < value_end; ++digit) {
                if (*digit < '0' || *digit > '9') return false;
                if (parsed > (std::numeric_limits<std::size_t>::max() - static_cast<std::size_t>(*digit - '0')) / 10U) return false;
                parsed = parsed * 10U + static_cast<std::size_t>(*digit - '0');
                if (parsed > maximum_body_bytes) return false;
            }
            framing.content_length = parsed;
        } else if (nameEquals(cursor, colon, "transfer-encoding")) {
            if (transfer_encoding_seen || !nameEquals(value_begin, value_end, "chunked")) return false;
            transfer_encoding_seen = true;
            framing.chunked = true;
        }
        cursor = line_end + 2;
    }
    return framing.has_content_length != framing.chunked
        && (framing.chunked || framing.content_length != 0U);
}
