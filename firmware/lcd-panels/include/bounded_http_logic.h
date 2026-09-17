#pragma once

#include <cstddef>

struct HttpResponseFraming {
    bool chunked = false;
    bool has_content_length = false;
    std::size_t content_length = 0U;
};

bool panelParseResponseHeaders(const char* data, std::size_t length, std::size_t maximum_header_bytes,
                               std::size_t maximum_body_bytes, HttpResponseFraming& framing);
