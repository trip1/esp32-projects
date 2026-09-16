#pragma once

#include "dashboard_logic.h"
#include "panel_logic.h"

struct DashboardConfig {
    PanelConfig sources{};
    char timezone[65]{};
    DashboardSchedule schedule{};
};
