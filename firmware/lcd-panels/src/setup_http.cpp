#include "setup_http.h"

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

const char* findHeaderEnd(const char* data, std::size_t length) {
    for (std::size_t index = 3U; index < length; ++index) {
        if (data[index - 3U] == '\r' && data[index - 2U] == '\n'
            && data[index - 1U] == '\r' && data[index] == '\n') return data + index + 1U;
    }
    return nullptr;
}

bool equalHeaderName(const char* begin, const char* end, const char* expected) {
    const std::size_t length = static_cast<std::size_t>(end - begin);
    if (length != std::strlen(expected)) return false;
    for (std::size_t index = 0U; index < length; ++index) {
        if (std::tolower(static_cast<unsigned char>(begin[index]))
            != std::tolower(static_cast<unsigned char>(expected[index]))) return false;
    }
    return true;
}

bool equalValue(const char* begin, const char* end, const char* expected) {
    return static_cast<std::size_t>(end - begin) == std::strlen(expected)
        && std::memcmp(begin, expected, std::strlen(expected)) == 0;
}

bool validOrigin(const char* begin, const char* end) {
    return equalValue(begin, end, "http://192.168.4.1") || equalValue(begin, end, "http://192.168.4.1:80");
}

bool validReferer(const char* begin, const char* end) {
    constexpr char standard[] = "http://192.168.4.1/";
    constexpr char default_port[] = "http://192.168.4.1:80/";
    const std::size_t length = static_cast<std::size_t>(end - begin);
    return (length >= sizeof(standard) - 1U && std::memcmp(begin, standard, sizeof(standard) - 1U) == 0)
        || (length >= sizeof(default_port) - 1U && std::memcmp(begin, default_port, sizeof(default_port) - 1U) == 0);
}

bool copyToken(const char* begin, const char* end, char* output, std::size_t capacity) {
    const std::size_t length = static_cast<std::size_t>(end - begin);
    if (length == 0U || length >= capacity) return false;
    std::memcpy(output, begin, length);
    output[length] = '\0';
    return true;
}
}  // namespace

SetupHttpResult setupHttpParse(const char* data, std::size_t length, std::size_t maximum_header_bytes,
                               std::size_t maximum_body_bytes, SetupHttpRequest& request) {
    request = {};
    if (data == nullptr || maximum_header_bytes < 4U || length == 0U) return SetupHttpResult::NeedMore;
    const std::size_t searchable = length < maximum_header_bytes ? length : maximum_header_bytes;
    const char* header_limit = data + searchable;
    const char* header_end_pointer = findHeaderEnd(data, searchable);
    if (header_end_pointer == nullptr) return length >= maximum_header_bytes ? SetupHttpResult::HeaderTooLarge : SetupHttpResult::NeedMore;
    request.header_end = static_cast<std::size_t>(header_end_pointer - data);
    if (std::memchr(data, '\0', request.header_end) != nullptr) return SetupHttpResult::Malformed;

    const char* request_line_end = findCrlf(data, header_limit);
    if (request_line_end == nullptr) return SetupHttpResult::Malformed;
    const char* first_space = static_cast<const char*>(std::memchr(data, ' ', static_cast<std::size_t>(request_line_end - data)));
    const char* second_space = first_space == nullptr ? nullptr : static_cast<const char*>(
        std::memchr(first_space + 1, ' ', static_cast<std::size_t>(request_line_end - first_space - 1)));
    if (first_space == nullptr || second_space == nullptr
        || std::memchr(second_space + 1, ' ', static_cast<std::size_t>(request_line_end - second_space - 1)) != nullptr
        || !copyToken(data, first_space, request.method, sizeof(request.method))
        || !copyToken(first_space + 1, second_space, request.target, sizeof(request.target))
        || (!equalValue(second_space + 1, request_line_end, "HTTP/1.1")
            && !equalValue(second_space + 1, request_line_end, "HTTP/1.0"))
        || request.target[0] != '/') return SetupHttpResult::Malformed;
    if (std::strcmp(request.method, "GET") != 0 && std::strcmp(request.method, "POST") != 0) return SetupHttpResult::Malformed;
    for (const char* cursor = first_space + 1; cursor < second_space; ++cursor) {
        const unsigned char value = static_cast<unsigned char>(*cursor);
        if (value <= 0x20U || value == 0x7fU) return SetupHttpResult::Malformed;
    }

    bool host_seen = false;
    bool length_seen = false;
    const bool state_changing = std::strcmp(request.method, "POST") == 0;
    const char* cursor = request_line_end + 2;
    while (cursor < header_end_pointer - 2) {
        const char* line_end = findCrlf(cursor, header_end_pointer);
        if (line_end == nullptr || line_end == cursor) return SetupHttpResult::Malformed;
        const char* colon = static_cast<const char*>(std::memchr(cursor, ':', static_cast<std::size_t>(line_end - cursor)));
        if (colon == nullptr || colon == cursor) return SetupHttpResult::Malformed;
        for (const char* name = cursor; name < colon; ++name) {
            if (!std::isalnum(static_cast<unsigned char>(*name)) && *name != '-') return SetupHttpResult::Malformed;
        }
        const char* value_begin = colon + 1;
        while (value_begin < line_end && (*value_begin == ' ' || *value_begin == '\t')) ++value_begin;
        const char* value_end = line_end;
        while (value_end > value_begin && (value_end[-1] == ' ' || value_end[-1] == '\t')) --value_end;
        if (value_begin == value_end) return SetupHttpResult::Malformed;
        for (const char* value = value_begin; value < value_end; ++value) {
            const unsigned char character = static_cast<unsigned char>(*value);
            if ((character < 0x20U && character != '\t') || character == 0x7fU) return SetupHttpResult::Malformed;
        }

        if (equalHeaderName(cursor, colon, "transfer-encoding")) return SetupHttpResult::PolicyRejected;
        if (equalHeaderName(cursor, colon, "host")) {
            if (host_seen) return SetupHttpResult::PolicyRejected;
            host_seen = true;
            request.canonical_host = equalValue(value_begin, value_end, "192.168.4.1")
                || equalValue(value_begin, value_end, "192.168.4.1:80");
        } else if (equalHeaderName(cursor, colon, "origin")) {
            if (request.has_origin || (state_changing && !validOrigin(value_begin, value_end))) return SetupHttpResult::PolicyRejected;
            request.has_origin = true;
        } else if (equalHeaderName(cursor, colon, "referer")) {
            if (request.has_referer || (state_changing && !validReferer(value_begin, value_end))) return SetupHttpResult::PolicyRejected;
            request.has_referer = true;
        } else if (equalHeaderName(cursor, colon, "content-length")) {
            if (length_seen) return SetupHttpResult::PolicyRejected;
            length_seen = true;
            std::size_t parsed = 0U;
            for (const char* digit = value_begin; digit < value_end; ++digit) {
                if (*digit < '0' || *digit > '9') return SetupHttpResult::Malformed;
                if (parsed > (std::numeric_limits<std::size_t>::max() - static_cast<std::size_t>(*digit - '0')) / 10U)
                    return SetupHttpResult::BodyTooLarge;
                parsed = parsed * 10U + static_cast<std::size_t>(*digit - '0');
                if (parsed > maximum_body_bytes) return SetupHttpResult::BodyTooLarge;
            }
            request.content_length = parsed;
        }
        cursor = line_end + 2;
    }

    if (!host_seen) return SetupHttpResult::PolicyRejected;
    if (state_changing && (!request.canonical_host || (!request.has_origin && !request.has_referer) || !length_seen))
        return SetupHttpResult::PolicyRejected;
    if (!state_changing && request.content_length != 0U) return SetupHttpResult::PolicyRejected;
    if (request.header_end > std::numeric_limits<std::size_t>::max() - request.content_length)
        return SetupHttpResult::BodyTooLarge;
    request.total_length = request.header_end + request.content_length;
    if (length < request.total_length) return SetupHttpResult::NeedMore;
    if (length != request.total_length) return SetupHttpResult::Malformed;
    return SetupHttpResult::Complete;
}

SetupHttpRoute setupHttpRoute(const SetupHttpRequest& request) {
    if (std::strcmp(request.method, "POST") == 0) {
        return std::strcmp(request.target, "/save") == 0 ? SetupHttpRoute::Save : SetupHttpRoute::NotFound;
    }
    if (std::strcmp(request.method, "GET") == 0 && request.canonical_host && std::strcmp(request.target, "/") == 0)
        return SetupHttpRoute::SetupPage;
    if (std::strcmp(request.method, "GET") == 0) return SetupHttpRoute::CaptiveRedirect;
    return SetupHttpRoute::NotFound;
}

bool setupHttpCanRead(bool connected, std::size_t available_bytes) { return connected || available_bytes != 0U; }

const char* setupHttpResultName(SetupHttpResult result) {
    switch (result) {
        case SetupHttpResult::NeedMore: return "incomplete";
        case SetupHttpResult::Complete: return "complete";
        case SetupHttpResult::Malformed: return "malformed";
        case SetupHttpResult::HeaderTooLarge: return "header-too-large";
        case SetupHttpResult::BodyTooLarge: return "body-too-large";
        case SetupHttpResult::PolicyRejected: return "policy-rejected";
    }
    return "unknown";
}
