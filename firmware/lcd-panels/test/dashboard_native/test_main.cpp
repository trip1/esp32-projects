#include "dashboard_logic.h"

#include <cassert>
#include <cstdint>

int main() {
    DashboardSchedule schedule{};
    schedule.order[0] = DashboardScreen::Clock;
    schedule.order[1] = DashboardScreen::Mqtt;
    schedule.order[2] = DashboardScreen::Unifi;
    schedule.order[3] = DashboardScreen::Satellite;
    schedule.order[4] = DashboardScreen::Weather;
    schedule.duration_seconds[0] = 10U;
    schedule.duration_seconds[1] = 20U;
    schedule.duration_seconds[2] = 30U;
    schedule.duration_seconds[3] = 40U;
    schedule.duration_seconds[4] = 50U;
    assert(dashboardScheduleValid(schedule));
    assert(dashboardNextSlot(schedule, 0U) == 1U);
    assert(dashboardNextSlot(schedule, 4U) == 0U);
    assert(dashboardDurationForSlot(schedule, 1U) == 20U);
    schedule.order[0] = DashboardScreen::Weather;
    schedule.order[4] = DashboardScreen::Clock;
    assert(dashboardDurationForSlot(schedule, 0U) == 50U);
    assert(dashboardDurationForSlot(schedule, 4U) == 10U);
    schedule.order[0] = DashboardScreen::Clock;
    schedule.order[4] = DashboardScreen::Weather;
    assert(!dashboardSlotExpired(1000U, 10999U, 10U));
    assert(dashboardSlotExpired(1000U, 11000U, 10U));
    assert(!dashboardSlotExpired(UINT32_MAX - 3000U, 4998U, 8U));
    assert(dashboardSlotExpired(UINT32_MAX - 3000U, 4999U, 8U));

    schedule.order[4] = DashboardScreen::Clock;
    assert(!dashboardScheduleValid(schedule));
    schedule.order[4] = DashboardScreen::Weather;
    schedule.duration_seconds[2] = 4U;
    assert(!dashboardScheduleValid(schedule));
    schedule.duration_seconds[2] = 3601U;
    assert(!dashboardScheduleValid(schedule));

    DashboardScreen parsed{};
    assert(dashboardParseScreen("clock", parsed) && parsed == DashboardScreen::Clock);
    assert(dashboardParseScreen("mqtt", parsed) && parsed == DashboardScreen::Mqtt);
    assert(dashboardParseScreen("unifi", parsed) && parsed == DashboardScreen::Unifi);
    assert(dashboardParseScreen("satellite", parsed) && parsed == DashboardScreen::Satellite);
    assert(dashboardParseScreen("weather", parsed) && parsed == DashboardScreen::Weather);
    assert(!dashboardParseScreen("admin", parsed));
    assert(dashboardScreenName(DashboardScreen::Satellite)[0] == 's');
    assert(dashboardTimezoneValid("CST6CDT,M3.2.0,M11.1.0"));
    assert(dashboardTimezoneValid("UTC0"));
    assert(!dashboardTimezoneValid("../../etc/passwd"));
    return 0;
}
