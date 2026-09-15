#include "dashboard_logic.h"

#include <cstring>

bool dashboardScheduleValid(const DashboardSchedule& value) {
    bool seen[kDashboardScreenCount]{};
    for (std::size_t index = 0U; index < kDashboardScreenCount; ++index) {
        const auto screen = static_cast<std::uint8_t>(value.order[index]);
        if (screen >= kDashboardScreenCount || seen[screen] || value.duration_seconds[index] < 5U || value.duration_seconds[index] > 3600U) return false;
        seen[screen] = true;
    }
    return true;
}

std::size_t dashboardNextSlot(const DashboardSchedule& value, std::size_t current_slot) {
    if (!dashboardScheduleValid(value) || current_slot >= kDashboardScreenCount) return 0U;
    return (current_slot + 1U) % kDashboardScreenCount;
}

std::uint16_t dashboardDurationForSlot(const DashboardSchedule& value, std::size_t slot) {
    if (!dashboardScheduleValid(value) || slot >= kDashboardScreenCount) return 0U;
    return value.duration_seconds[static_cast<std::uint8_t>(value.order[slot])];
}

bool dashboardSlotExpired(std::uint32_t started_ms, std::uint32_t now_ms, std::uint16_t duration_seconds) {
    if (duration_seconds < 5U || duration_seconds > 3600U) return false;
    return static_cast<std::uint32_t>(now_ms - started_ms) >= static_cast<std::uint32_t>(duration_seconds) * 1000U;
}

const char* dashboardScreenName(DashboardScreen value) {
    switch (value) {
        case DashboardScreen::Clock: return "clock";
        case DashboardScreen::Satellite: return "satellite";
        case DashboardScreen::Weather: return "weather";
        case DashboardScreen::Launch: return "launch";
        case DashboardScreen::Moon: return "moon";
        case DashboardScreen::Solar: return "solar";
        case DashboardScreen::Planet: return "planet";
        case DashboardScreen::Neo: return "neo";
        case DashboardScreen::DeepSpace: return "deep-space";
    }
    return "";
}

bool dashboardParseScreen(const char* value, DashboardScreen& output) {
    if (value == nullptr) return false;
    for (std::uint8_t raw = 0U; raw < kDashboardScreenCount; ++raw) {
        const auto candidate = static_cast<DashboardScreen>(raw);
        if (std::strcmp(value, dashboardScreenName(candidate)) == 0) {
            output = candidate;
            return true;
        }
    }
    return false;
}

bool dashboardMigrateLegacySchedule(const std::uint8_t* legacy_order, const std::uint16_t* legacy_durations,
                                    std::size_t legacy_count, DashboardSchedule& output) {
    if (legacy_order == nullptr || legacy_durations == nullptr || (legacy_count != 4U && legacy_count != 10U)) return false;
    bool legacy_seen[10]{};
    for (std::size_t index = 0U; index < legacy_count; ++index) {
        if (legacy_order[index] >= legacy_count || legacy_seen[legacy_order[index]]) return false;
        legacy_seen[legacy_order[index]] = true;
    }
    for (std::size_t index = 0U; index < kDashboardScreenCount; ++index) output.duration_seconds[index] = 15U;
    for (std::uint8_t legacy = 0U; legacy < legacy_count; ++legacy) {
        if (legacy == 1U) continue;
        const std::uint8_t current = legacy == 0U ? 0U : static_cast<std::uint8_t>(legacy - 1U);
        if (legacy_durations[legacy] < 5U || legacy_durations[legacy] > 3600U) return false;
        output.duration_seconds[current] = legacy_durations[legacy];
    }
    bool current_seen[kDashboardScreenCount]{};
    std::size_t written = 0U;
    for (std::size_t index = 0U; index < legacy_count; ++index) {
        const std::uint8_t legacy = legacy_order[index];
        if (legacy == 1U) continue;
        const std::uint8_t current = legacy == 0U ? 0U : static_cast<std::uint8_t>(legacy - 1U);
        output.order[written++] = static_cast<DashboardScreen>(current);
        current_seen[current] = true;
    }
    for (std::uint8_t current = 0U; current < kDashboardScreenCount; ++current) {
        if (!current_seen[current]) output.order[written++] = static_cast<DashboardScreen>(current);
    }
    return written == kDashboardScreenCount && dashboardScheduleValid(output);
}

bool dashboardTimezoneValid(const char* value) {
    if (value == nullptr) return false;
    static constexpr const char* allowed[] = {
        "CST6CDT,M3.2.0,M11.1.0", "EST5EDT,M3.2.0,M11.1.0", "MST7MDT,M3.2.0,M11.1.0",
        "MST7", "PST8PDT,M3.2.0,M11.1.0", "AKST9AKDT,M3.2.0,M11.1.0", "HST10", "UTC0",
        "GMT0BST,M3.5.0/1,M10.5.0", "CET-1CEST,M3.5.0,M10.5.0/3",
        "AEST-10AEDT,M10.1.0,M4.1.0/3", "JST-9", "IST-5:30",
    };
    for (const char* candidate : allowed) if (std::strcmp(value, candidate) == 0) return true;
    return false;
}
