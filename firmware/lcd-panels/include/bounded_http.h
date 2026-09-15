#pragma once

#include <cstddef>

bool panelHttpGetBounded(const char* url, const char* ca_certificate, char* output, std::size_t capacity);
bool panelHttpGetBoundedAt(const char* url, const char* ca_certificate, double latitude, double longitude, char* output, std::size_t capacity);
