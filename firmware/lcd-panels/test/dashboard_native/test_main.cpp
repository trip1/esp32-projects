#include "dashboard_logic.h"
#include "dashboard_setup.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

int main() {
    static_assert(kDashboardScreenCount == 9U);
    DashboardSchedule schedule{};
    const DashboardScreen screens[kDashboardScreenCount] = {
        DashboardScreen::Clock, DashboardScreen::Satellite, DashboardScreen::Weather,
        DashboardScreen::Launch, DashboardScreen::Moon, DashboardScreen::Solar,
        DashboardScreen::Planet, DashboardScreen::Neo, DashboardScreen::DeepSpace,
    };
    for (std::size_t index = 0U; index < kDashboardScreenCount; ++index) {
        schedule.order[index] = screens[index];
        schedule.duration_seconds[index] = static_cast<std::uint16_t>(10U + index * 5U);
    }
    assert(dashboardScheduleValid(schedule));
    assert(dashboardNextSlot(schedule, 0U) == 1U);
    assert(dashboardNextSlot(schedule, 8U) == 0U);
    assert(dashboardDurationForSlot(schedule, 1U) == 15U);
    schedule.order[0] = DashboardScreen::DeepSpace;
    schedule.order[8] = DashboardScreen::Clock;
    assert(dashboardDurationForSlot(schedule, 0U) == 50U);
    assert(dashboardDurationForSlot(schedule, 8U) == 10U);
    schedule.order[0] = DashboardScreen::Clock;
    schedule.order[8] = DashboardScreen::DeepSpace;
    assert(!dashboardSlotExpired(1000U, 10999U, 10U));
    assert(dashboardSlotExpired(1000U, 11000U, 10U));
    assert(!dashboardSlotExpired(UINT32_MAX - 3000U, 4998U, 8U));
    assert(dashboardSlotExpired(UINT32_MAX - 3000U, 4999U, 8U));

    schedule.order[8] = DashboardScreen::Clock;
    assert(!dashboardScheduleValid(schedule));
    schedule.order[8] = DashboardScreen::DeepSpace;
    schedule.duration_seconds[2] = 4U;
    assert(!dashboardScheduleValid(schedule));
    schedule.duration_seconds[2] = 3601U;
    assert(!dashboardScheduleValid(schedule));

    DashboardScreen parsed{};
    for (const char* name : {"clock", "satellite", "weather", "launch", "moon", "solar", "planet", "neo", "deep-space"}) {
        assert(dashboardParseScreen(name, parsed));
    }
    assert(!dashboardParseScreen("mqtt", parsed));
    assert(!dashboardParseScreen("unifi", parsed));
    assert(!dashboardParseScreen("admin", parsed));
    assert(dashboardScreenName(DashboardScreen::DeepSpace)[0] == 'd');
    assert(dashboardTimezoneValid("CST6CDT,M3.2.0,M11.1.0"));
    assert(dashboardTimezoneValid("UTC0"));
    assert(!dashboardTimezoneValid("../../etc/passwd"));

    const std::uint8_t legacy_v3_order[10] = {1U, 4U, 0U, 9U, 2U, 3U, 5U, 6U, 7U, 8U};
    const std::uint16_t legacy_v3_durations[10] = {10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U, 18U, 19U};
    DashboardSchedule migrated{};
    assert(dashboardMigrateLegacySchedule(legacy_v3_order, legacy_v3_durations, 10U, migrated));
    assert(migrated.order[0] == DashboardScreen::Launch);
    assert(migrated.order[1] == DashboardScreen::Clock);
    assert(migrated.duration_seconds[static_cast<std::uint8_t>(DashboardScreen::Clock)] == 10U);
    assert(migrated.duration_seconds[static_cast<std::uint8_t>(DashboardScreen::Satellite)] == 12U);
    assert(migrated.duration_seconds[static_cast<std::uint8_t>(DashboardScreen::DeepSpace)] == 19U);
    assert(dashboardScheduleValid(migrated));

    const std::uint8_t legacy_v2_order[4] = {0U, 1U, 2U, 3U};
    const std::uint16_t legacy_v2_durations[4] = {20U, 21U, 22U, 23U};
    assert(dashboardMigrateLegacySchedule(legacy_v2_order, legacy_v2_durations, 4U, migrated));
    assert(migrated.order[0] == DashboardScreen::Clock);
    assert(migrated.order[1] == DashboardScreen::Satellite);
    assert(migrated.order[2] == DashboardScreen::Weather);
    assert(migrated.order[3] == DashboardScreen::Launch);
    assert(migrated.duration_seconds[static_cast<std::uint8_t>(DashboardScreen::Weather)] == 23U);
    assert(!dashboardMigrateLegacySchedule(legacy_v2_order, legacy_v2_durations, 3U, migrated));

    const char setup_body[] = "csrf=0123456789abcdef&wifi_ssid=HomeWiFi&wifi_password=ppppppppppppppppppppppppppppppppppp&latitude=32.5&longitude=-94.7&timezone=CST6CDT%2CM3.2.0%2CM11.1.0&order_1=clock&order_2=satellite&order_3=weather&order_4=launch&order_5=moon&order_6=solar&order_7=planet&order_8=neo&order_9=deep-space&duration_clock=15&duration_satellite=15&duration_weather=15&duration_launch=15&duration_moon=15&duration_solar=15&duration_planet=15&duration_neo=15&duration_deep-space=15";
    static_assert(sizeof(setup_body) - 1U == 464U);
    DashboardSetupFields setup{};
    assert(dashboardParseSetupForm(setup_body, sizeof(setup_body) - 1U, setup));
    assert(std::strcmp(setup.csrf, "0123456789abcdef") == 0);
    assert(std::strcmp(setup.value.sources.wifi_ssid, "HomeWiFi") == 0);
    assert(std::strcmp(setup.value.sources.wifi_password, "ppppppppppppppppppppppppppppppppppp") == 0);
    assert(std::strcmp(setup.value.timezone, "CST6CDT,M3.2.0,M11.1.0") == 0);
    assert(setup.value.schedule.order[8] == DashboardScreen::DeepSpace);
    assert(setup.value.schedule.duration_seconds[8] == 15U);
    std::string blank_password(setup_body);
    const std::string password_value = "ppppppppppppppppppppppppppppppppppp";
    blank_password.erase(blank_password.find(password_value), password_value.size());
    assert(dashboardParseSetupForm(blank_password.data(), blank_password.size(), setup));
    assert(setup.value.sources.wifi_password[0] == '\0');
    const std::string duplicate_setup_field = std::string(setup_body) + "&wifi_ssid=Other";
    assert(!dashboardParseSetupForm(duplicate_setup_field.data(), duplicate_setup_field.size(), setup));
    return 0;
}
