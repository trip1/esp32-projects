#include "dashboard_logic.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>

int main() {
    static_assert(kDashboardScreenCount == 10U);
    DashboardSchedule schedule{};
    const DashboardScreen screens[kDashboardScreenCount] = {
        DashboardScreen::Clock, DashboardScreen::Mqtt, DashboardScreen::Satellite, DashboardScreen::Weather,
        DashboardScreen::Launch, DashboardScreen::Moon, DashboardScreen::Solar, DashboardScreen::Planet,
        DashboardScreen::Neo, DashboardScreen::DeepSpace,
    };
    for (std::size_t index = 0U; index < kDashboardScreenCount; ++index) {
        schedule.order[index] = screens[index];
        schedule.duration_seconds[index] = static_cast<std::uint16_t>(10U + index * 5U);
    }
    assert(dashboardScheduleValid(schedule));
    assert(dashboardNextSlot(schedule, 0U) == 1U);
    assert(dashboardNextSlot(schedule, 9U) == 0U);
    assert(dashboardDurationForSlot(schedule, 1U) == 15U);
    schedule.order[0] = DashboardScreen::DeepSpace;
    schedule.order[9] = DashboardScreen::Clock;
    assert(dashboardDurationForSlot(schedule, 0U) == 55U);
    assert(dashboardDurationForSlot(schedule, 9U) == 10U);
    schedule.order[0] = DashboardScreen::Clock;
    schedule.order[9] = DashboardScreen::DeepSpace;
    assert(!dashboardSlotExpired(1000U, 10999U, 10U));
    assert(dashboardSlotExpired(1000U, 11000U, 10U));
    assert(!dashboardSlotExpired(UINT32_MAX - 3000U, 4998U, 8U));
    assert(dashboardSlotExpired(UINT32_MAX - 3000U, 4999U, 8U));

    schedule.order[9] = DashboardScreen::Clock;
    assert(!dashboardScheduleValid(schedule));
    schedule.order[9] = DashboardScreen::DeepSpace;
    schedule.duration_seconds[2] = 4U;
    assert(!dashboardScheduleValid(schedule));
    schedule.duration_seconds[2] = 3601U;
    assert(!dashboardScheduleValid(schedule));

    DashboardScreen parsed{};
    for (const char* name : {"clock", "mqtt", "satellite", "weather", "launch", "moon", "solar", "planet", "neo", "deep-space"}) {
        assert(dashboardParseScreen(name, parsed));
    }
    assert(!dashboardParseScreen("unifi", parsed));
    assert(!dashboardParseScreen("admin", parsed));
    assert(dashboardScreenName(DashboardScreen::DeepSpace)[0] == 'd');
    assert(dashboardTimezoneValid("CST6CDT,M3.2.0,M11.1.0"));
    assert(dashboardTimezoneValid("UTC0"));
    assert(!dashboardTimezoneValid("../../etc/passwd"));
    return 0;
}
