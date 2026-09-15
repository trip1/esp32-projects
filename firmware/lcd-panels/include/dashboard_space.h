#pragma once

#include <ArduinoJson.h>

bool dashboardSpaceResponseValid(const JsonDocument& document, long long now_unix);
