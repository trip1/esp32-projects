#include "dashboard_space.h"

#include <cmath>
#include <cstring>

namespace {
bool exactObject(JsonObjectConst object, size_t size, const char* const* keys) {
    if (object.isNull() || object.size() != size) return false;
    for (size_t index = 0; index < size; ++index) {
        if (object[keys[index]].isNull()) return false;
    }
    return true;
}

bool integer(JsonVariantConst value) { return value.is<long long>(); }
bool number(JsonVariantConst value) {
    if (!value.is<double>()) return false;
    return std::isfinite(value.as<double>());
}
bool text(JsonVariantConst value) { return value.is<const char*>(); }
bool availability(JsonObjectConst value) { return value["available"].is<bool>(); }
}

bool dashboardSpaceResponseValid(const JsonDocument& document, long long now_unix) {
    static const char* const rootKeys[] = {"status", "data", "timestamp"};
    JsonObjectConst root = document.as<JsonObjectConst>();
    if (!exactObject(root, 3U, rootKeys) || !text(root["status"]) || std::strcmp(root["status"].as<const char*>(), "ok") != 0 || !text(root["timestamp"]) || !root["data"].is<JsonObjectConst>()) return false;

    static const char* const dataKeys[] = {"v", "observed_at", "launch", "moon", "solar", "planet", "neo", "mission"};
    JsonObjectConst data = root["data"].as<JsonObjectConst>();
    if (!exactObject(data, 8U, dataKeys) || !integer(data["v"]) || data["v"].as<long long>() != 1LL || !integer(data["observed_at"])) return false;
    const long long observed = data["observed_at"].as<long long>();
    const long long age = now_unix >= observed ? now_unix - observed : observed - now_unix;
    if (observed < 1704067200LL || observed > 4102444800LL || now_unix < 1704067200LL || now_unix > 4102444800LL || age > 900LL) return false;

    static const char* const launchKeys[] = {"available", "name", "net", "status"};
    JsonObjectConst launch = data["launch"].as<JsonObjectConst>();
    if (!exactObject(launch, 4U, launchKeys) || !availability(launch) || !text(launch["name"]) || !integer(launch["net"]) || !text(launch["status"])) return false;

    static const char* const moonKeys[] = {"available", "phase", "illumination"};
    JsonObjectConst moon = data["moon"].as<JsonObjectConst>();
    if (!exactObject(moon, 3U, moonKeys) || !availability(moon) || !text(moon["phase"]) || !integer(moon["illumination"])) return false;

    static const char* const solarKeys[] = {"available", "kp", "level", "observed_at"};
    JsonObjectConst solar = data["solar"].as<JsonObjectConst>();
    if (!exactObject(solar, 4U, solarKeys) || !availability(solar) || !number(solar["kp"]) || !text(solar["level"]) || !integer(solar["observed_at"])) return false;

    static const char* const planetKeys[] = {"available", "name", "altitude", "azimuth", "direction"};
    JsonObjectConst planet = data["planet"].as<JsonObjectConst>();
    if (!exactObject(planet, 5U, planetKeys) || !availability(planet) || !text(planet["name"]) || !integer(planet["altitude"]) || !integer(planet["azimuth"]) || !text(planet["direction"])) return false;

    static const char* const neoKeys[] = {"available", "name", "approach_at", "distance_ld"};
    JsonObjectConst neo = data["neo"].as<JsonObjectConst>();
    if (!exactObject(neo, 4U, neoKeys) || !availability(neo) || !text(neo["name"]) || !integer(neo["approach_at"]) || !number(neo["distance_ld"])) return false;

    static const char* const missionKeys[] = {"available", "name", "distance_au", "mission_days"};
    JsonObjectConst mission = data["mission"].as<JsonObjectConst>();
    return exactObject(mission, 4U, missionKeys) && availability(mission) && text(mission["name"]) && number(mission["distance_au"]) && integer(mission["mission_days"]);
}
