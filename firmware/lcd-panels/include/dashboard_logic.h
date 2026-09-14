#pragma once

#include <cstddef>
#include <cstdint>

enum class DashboardScreen : std::uint8_t { Clock = 0, Mqtt = 1, Unifi = 2, Satellite = 3, Weather = 4 };

struct DashboardSchedule {
    DashboardScreen order[5]{};
    std::uint16_t duration_seconds[5]{};
};

bool dashboardScheduleValid(const DashboardSchedule& value);
std::size_t dashboardNextSlot(const DashboardSchedule& value, std::size_t current_slot);
std::uint16_t dashboardDurationForSlot(const DashboardSchedule& value, std::size_t slot);
bool dashboardSlotExpired(std::uint32_t started_ms, std::uint32_t now_ms, std::uint16_t duration_seconds);
bool dashboardParseScreen(const char* value, DashboardScreen& output);
const char* dashboardScreenName(DashboardScreen value);
bool dashboardTimezoneValid(const char* value);
