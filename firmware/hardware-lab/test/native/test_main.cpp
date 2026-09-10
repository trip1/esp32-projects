#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>

#include "hardware_logic.h"

int main() {
    assert(std::fabs(distanceCentimetersFromEcho(5831U) - 100.0F) < 0.1F);
    assert(std::isnan(distanceCentimetersFromEcho(0U)));
    assert(std::isnan(distanceCentimetersFromEcho(30001U)));

    const float distances[] = {100.0F, 20.0F, 50.0F, 40.0F, 30.0F};
    assert(medianDistance(distances, 5U) == 40.0F);
    const float sparse[] = {NAN, 15.0F, NAN, 5.0F};
    assert(medianDistance(sparse, 4U) == 10.0F);
    const float oversized[] = {1, 2, 3, 4, 5, 6};
    assert(std::isnan(medianDistance(oversized, 6U)));

    assert(!occupancyActive(100U, 0U, 30000U, false));
    assert(occupancyActive(2000U, 1000U, 30000U, true));
    assert(!occupancyActive(31001U, 1000U, 30000U, true));
    assert(occupancyActive(10U, 0xfffffff0U, 100U, true));
    assert(saturatingEventAdd(10U, 3U) == 13U);
    assert(saturatingEventAdd(0xfffffffeU, 2U) == 0xffffffffU);

    assert(validWifiSsid("Home WiFi"));
    assert(!validWifiSsid(""));
    assert(!validWifiSsid(std::string(33, 'x')));
    assert(validWifiPassword("12345678"));
    assert(validWifiPassword(""));
    assert(!validWifiPassword("short"));
    assert(validPosixTimezone("CST6CDT,M3.2.0,M11.1.0"));
    assert(validPosixTimezone("UTC0"));
    assert(!validPosixTimezone("garbage"));
    assert(!validPosixTimezone(""));
    assert(!validPosixTimezone("bad\nzone"));
    assert(!validPosixTimezone(std::string(65, 'x')));

    char method[8]{};
    char target[64]{};
    std::size_t content_length = 0U;
    bool has_content_length = false;
    const std::string request = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Length: 7\r\n\r\n";
    assert(hardwareParseHttpRequest(request.data(), request.size(), 512U, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    assert(std::string(method) == "POST" && std::string(target) == "/save" && content_length == 7U);

    const std::string hostile = "POST /save HTTP/1.1\r\nHost: attacker.example\r\nContent-Length: 7\r\n\r\n";
    assert(!hardwareParseHttpRequest(hostile.data(), hostile.size(), 512U, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    const std::string duplicate = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 7\r\nContent-Length: 7\r\n\r\n";
    assert(!hardwareParseHttpRequest(duplicate.data(), duplicate.size(), 512U, method, sizeof(method), target, sizeof(target), content_length, has_content_length));

    static constexpr std::uint8_t bytes[] = "123456789";
    assert(hardwareConfigCrc32(bytes, sizeof(bytes) - 1U) == 0xcbf43926U);
    return 0;
}
