#pragma once

#include <cstddef>

#include "dashboard_types.h"

struct DashboardSetupFields {
    DashboardConfig value{};
    char csrf[17]{};
};

bool dashboardParseSetupForm(const char* body, std::size_t length, DashboardSetupFields& output);
