#include <ArduinoJson.h>
#include <cassert>
#include <cstring>

#include "dashboard_space.h"

namespace {
const char* validPayload = R"JSON({"status":"ok","data":{"v":1,"observed_at":1789452000,"launch":{"available":true,"name":"Mission","net":1789561800,"status":"Go"},"moon":{"available":true,"phase":"Waxing","illumination":0},"solar":{"available":true,"kp":0,"level":"quiet","observed_at":1789452000},"planet":{"available":true,"name":"Mars","altitude":0,"azimuth":0,"direction":"N"},"neo":{"available":false,"name":"","approach_at":0,"distance_ld":0},"mission":{"available":true,"name":"Voyager 1","distance_au":166.2,"mission_days":18000}},"timestamp":"2026-09-15T06:00:00Z"})JSON";

bool accepts(const char* payload) {
    JsonDocument document;
    if (deserializeJson(document, payload) != DeserializationError::Ok) return false;
    return dashboardSpaceResponseValid(document, 1789452000LL);
}
}

int main() {
    assert(accepts(validPayload));
    assert(!accepts(R"JSON({"status":"ok","data":{"v":1},"timestamp":"x"})JSON"));

    JsonDocument document;
    assert(deserializeJson(document, validPayload) == DeserializationError::Ok);
    document["extra"] = 1;
    assert(!dashboardSpaceResponseValid(document, 1789452000LL));
    document.remove("extra");
    document["data"]["launch"]["extra"] = 1;
    assert(!dashboardSpaceResponseValid(document, 1789452000LL));
    document["data"]["launch"].remove("extra");
    document["data"]["moon"].remove("illumination");
    assert(!dashboardSpaceResponseValid(document, 1789452000LL));
    document["data"]["moon"]["illumination"] = "0";
    assert(!dashboardSpaceResponseValid(document, 1789452000LL));
    document["data"]["moon"]["illumination"] = 0;
    document["data"]["v"] = 2;
    assert(!dashboardSpaceResponseValid(document, 1789452000LL));
    document["data"]["v"] = 1;
    assert(!dashboardSpaceResponseValid(document, 1789454000LL));
    return 0;
}
