#pragma once

#include <cstddef>
#include <cstdint>

enum class DashboardScreen : std::uint8_t {
    Clock = 0, Satellite = 1, Weather = 2, Launch = 3,
    Moon = 4, Solar = 5, Planet = 6, Neo = 7, DeepSpace = 8,
};
constexpr std::size_t kDashboardScreenCount = 9U;

struct DashboardSchedule {
    DashboardScreen order[kDashboardScreenCount]{};
    std::uint16_t duration_seconds[kDashboardScreenCount]{};
};

bool dashboardScheduleValid(const DashboardSchedule& value);
std::size_t dashboardNextSlot(const DashboardSchedule& value, std::size_t current_slot);
std::uint16_t dashboardDurationForSlot(const DashboardSchedule& value, std::size_t slot);
bool dashboardSlotExpired(std::uint32_t started_ms, std::uint32_t now_ms, std::uint16_t duration_seconds);
bool dashboardParseScreen(const char* value, DashboardScreen& output);
const char* dashboardScreenName(DashboardScreen value);
bool dashboardTimezoneValid(const char* value);
bool dashboardMigrateLegacySchedule(const std::uint8_t* legacy_order, const std::uint16_t* legacy_durations,
                                    std::size_t legacy_count, DashboardSchedule& output);
