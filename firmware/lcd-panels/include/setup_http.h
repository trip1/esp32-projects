#pragma once

#include <cstddef>
#include <cstdint>

enum class SetupHttpResult : std::uint8_t {
    NeedMore,
    Complete,
    Malformed,
    HeaderTooLarge,
    BodyTooLarge,
    PolicyRejected,
};

enum class SetupHttpRoute : std::uint8_t {
    SetupPage,
    Save,
    CaptiveRedirect,
    NotFound,
};

struct SetupHttpRequest {
    char method[8]{};
    char target[64]{};
    std::size_t header_end = 0U;
    std::size_t content_length = 0U;
    std::size_t total_length = 0U;
    bool canonical_host = false;
    bool has_origin = false;
    bool has_referer = false;
};

SetupHttpResult setupHttpParse(const char* data, std::size_t length, std::size_t maximum_header_bytes,
                               std::size_t maximum_body_bytes, SetupHttpRequest& request);
SetupHttpRoute setupHttpRoute(const SetupHttpRequest& request);
bool setupHttpCanRead(bool connected, std::size_t available_bytes);
const char* setupHttpResultName(SetupHttpResult result);
