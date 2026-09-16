#include "panel_logic.h"
#include "lcd1602_i2c.h"
#include "setup_http.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <string>

namespace {
void expectLine(const char actual[17], const char* expected) {
    assert(std::strlen(actual) == 16U);
    assert(std::strcmp(actual, expected) == 0);
}
}

int main() {
    assert(lcd1602::kCandidateAddressCount == 16U);
    assert(lcd1602::candidateAddress(0U) == 0x20U);
    assert(lcd1602::candidateAddress(7U) == 0x27U);
    assert(lcd1602::candidateAddress(8U) == 0x38U);
    assert(lcd1602::candidateAddress(15U) == 0x3fU);
    const auto inland = lcd1602::scanAddresses([](std::uint8_t address) { return address == 0x27U; }, 0x27U);
    assert(inland.responders == 1U && inland.address == 0x27U && inland.selected);
    const auto alternate = lcd1602::scanAddresses([](std::uint8_t address) { return address == 0x3fU; }, 0x27U);
    assert(alternate.responders == 1U && !alternate.selected);
    const auto ambiguous = lcd1602::scanAddresses([](std::uint8_t address) { return address == 0x20U || address == 0x3fU; }, 0x27U);
    assert(ambiguous.responders == 2U && !ambiguous.selected);
    const auto preferred = lcd1602::scanAddresses([](std::uint8_t address) { return address == 0x20U || address == 0x27U; }, 0x27U);
    assert(preferred.responders == 2U && preferred.address == 0x27U && preferred.selected);

    char top[17]{};
    char bottom[17]{};

    formatMqttText("Living Room", "72.4 F / 44%", top, bottom);
    expectLine(top, "Living Room     ");
    expectLine(bottom, "72.4 F / 44%    ");
    formatMqttText("Bad\nLabel", "ok\x01value", top, bottom);
    expectLine(top, "Bad Label       ");
    expectLine(bottom, "ok value        ");

    formatWeather(23.4F, 45, "Clear", top, bottom);
    expectLine(top, "Weather 23.4 C  ");
    expectLine(bottom, "Hum 45% Clear   ");
    formatWeather(NAN, -1, "Unavailable", top, bottom);
    expectLine(top, "Weather offline ");
    expectLine(bottom, "No current data ");

    formatUnifi(true, 12, 24, 2, top, bottom);
    expectLine(top, "WAN UP    12 ms ");
    expectLine(bottom, "24 clients 2 AP ");
    formatUnifi(false, -1, -1, -1, top, bottom);
    expectLine(top, "WAN DOWN        ");
    expectLine(bottom, "Stats unavail.  ");

    formatSatellite(29.12, -95.23, 420, top, bottom);
    expectLine(top, "ISS 29.12 N     ");
    expectLine(bottom, "95.23 W 420 km  ");
    formatSatellite(NAN, NAN, -1, top, bottom);
    expectLine(top, "ISS unavailable ");
    expectLine(bottom, "Position unknown");

    assert(validExactMqttTopic("home/environment/sensor/state"));
    assert(!validExactMqttTopic("home/+/state"));
    assert(!validExactMqttTopic("home/#"));
    assert(!validExactMqttTopic("/leading"));
    assert(!validExactMqttTopic("trailing/"));
    assert(!validExactMqttTopic(std::string(129, 'a').c_str()));

    assert(validLatitude("32.5000"));
    assert(validLatitude("-90"));
    assert(!validLatitude("90.1"));
    assert(validLongitude("-180"));
    assert(validLongitude("180"));
    assert(!validLongitude("181"));
    assert(!validLongitude("12x"));

    assert(!mqttMessageStale(0U, 0U, true));
    assert(mqttMessageStale(120002U, 1U, true));
    assert(!mqttMessageStale(120002U, 1U, false));
    assert(!mqttMessageStale(2U, UINT32_MAX - 2U, true));

    char path[192]{};
    assert(buildWeatherPath(32.5, -94.74, path, sizeof(path)));
    assert(std::strcmp(path, "/v1/forecast?latitude=32.5000&longitude=-94.7400&current=temperature_2m,relative_humidity_2m,weather_code&temperature_unit=celsius&timeformat=unixtime") == 0);
    assert(std::strcmp(weatherCondition(0), "Clear") == 0);
    assert(std::strcmp(weatherCondition(3), "Cloudy") == 0);
    assert(std::strcmp(weatherCondition(61), "Rain") == 0);
    assert(std::strcmp(weatherCondition(95), "Storm") == 0);
    assert(std::strcmp(weatherCondition(500), "Unknown") == 0);

    PanelConfig config{};
    std::strcpy(config.wifi_ssid, "Home WiFi");
    std::strcpy(config.wifi_password, "password");
    assert(panelConfigValid(PanelKind::Space, config));
    std::strcpy(config.latitude, "32.5000");
    std::strcpy(config.longitude, "-94.7400");
    assert(panelConfigValid(PanelKind::Weather, config));
    std::strcpy(config.mqtt_host, "10.0.0.2");
    config.mqtt_port = 1883;
    std::strcpy(config.mqtt_topic, "home/environment/sensor/state");
    std::strcpy(config.label, "Living Room");
    assert(panelConfigValid(PanelKind::Mqtt, config));
    std::strcpy(config.mqtt_host, "broker.example.com");
    assert(!panelConfigValid(PanelKind::Mqtt, config));
    std::strcpy(config.mqtt_host, "10.0.0.2");
    config.mqtt_topic[0] = '#';
    assert(!panelConfigValid(PanelKind::Mqtt, config));
    config.mqtt_topic[0] = 'h';
    std::strcpy(config.unifi_url, "http://10.0.0.2:8090/api/unifi/summary");
    assert(panelConfigValid(PanelKind::Unifi, config));
    std::strcpy(config.unifi_url, "http://example.com/unifi");
    assert(!panelConfigValid(PanelKind::Unifi, config));
    std::strcpy(config.unifi_url, "https://example.com/api/unifi/summary");
    assert(!panelConfigValid(PanelKind::Unifi, config));
    std::strcpy(config.unifi_url, "https://example.com/api/unifi/summary?token=secret");
    assert(!panelConfigValid(PanelKind::Unifi, config));
    std::strcpy(config.unifi_url, "http://10.0.0.2:70000/api/unifi/summary");
    assert(!panelConfigValid(PanelKind::Unifi, config));
    std::strcpy(config.unifi_url, "http://10.0.0.2:1234/api/unifi/summary");
    assert(!panelConfigValid(PanelKind::Unifi, config));
    std::strcpy(config.unifi_url, "http://010.0.0.2:8090/api/unifi/summary");
    assert(!panelConfigValid(PanelKind::Unifi, config));
    std::strcpy(config.unifi_url, "http://10.0.0.2:08090/api/unifi/summary");
    assert(!panelConfigValid(PanelKind::Unifi, config));
    std::strcpy(config.unifi_url, "https://example.com/bad path");
    assert(!panelConfigValid(PanelKind::Unifi, config));
    std::strcpy(config.unifi_url, "https://example.com/admin");
    assert(!panelConfigValid(PanelKind::Unifi, config));

    static constexpr unsigned char checksum_input[] = "123456789";
    assert(panelCrc32(checksum_input, sizeof(checksum_input) - 1U) == 0xcbf43926U);

    SetupHttpRequest setup_request{};
    auto setupResult = [&](const std::string& value) {
        return setupHttpParse(value.data(), value.size(), 2048U, 3072U, setup_request);
    };
    const std::string body7 = "a=12345";
    const std::string request = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Length: 7\r\n\r\n" + body7;
    assert(setupResult(request) == SetupHttpResult::Complete);
    assert(std::strcmp(setup_request.method, "POST") == 0 && std::strcmp(setup_request.target, "/save") == 0 && setup_request.content_length == 7U);
    const std::string hostile = "POST /save HTTP/1.1\r\nHost: attacker.example\r\nOrigin: http://192.168.4.1\r\nContent-Length: 7\r\n\r\n" + body7;
    assert(setupResult(hostile) == SetupHttpResult::PolicyRejected);
    const std::string missing_origin = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 7\r\n\r\n" + body7;
    assert(setupResult(missing_origin) == SetupHttpResult::PolicyRejected);
    const std::string captive_referer_only = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nReferer: http://192.168.4.1/\r\nContent-Length: 7\r\n\r\n" + body7;
    assert(setupResult(captive_referer_only) == SetupHttpResult::Complete);
    const std::string default_port_origin = "POST /save HTTP/1.1\r\nHost: 192.168.4.1:80\r\nOrigin: http://192.168.4.1:80\r\nContent-Length: 7\r\n\r\n" + body7;
    assert(setupResult(default_port_origin) == SetupHttpResult::Complete);
    const std::string default_port_referer = "POST /save HTTP/1.1\r\nHost: 192.168.4.1:80\r\nReferer: http://192.168.4.1:80/\r\nContent-Length: 7\r\n\r\n" + body7;
    assert(setupResult(default_port_referer) == SetupHttpResult::Complete);
    const std::string foreign_referer = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nReferer: http://attacker.example/\r\nContent-Length: 7\r\n\r\n" + body7;
    assert(setupResult(foreign_referer) == SetupHttpResult::PolicyRejected);
    const std::string authority_confusion = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nReferer: http://192.168.4.1.attacker.example/\r\nContent-Length: 7\r\n\r\n" + body7;
    assert(setupResult(authority_confusion) == SetupHttpResult::PolicyRejected);
    const std::string duplicate_referer = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nReferer: http://192.168.4.1/\r\nReferer: http://192.168.4.1/\r\nContent-Length: 7\r\n\r\n" + body7;
    assert(setupResult(duplicate_referer) == SetupHttpResult::PolicyRejected);
    const std::string duplicate = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Length: 7\r\nContent-Length: 7\r\n\r\n" + body7;
    assert(setupResult(duplicate) == SetupHttpResult::PolicyRejected);
    const std::string duplicate_host = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Length: 7\r\n\r\n" + body7;
    assert(setupResult(duplicate_host) == SetupHttpResult::PolicyRejected);
    const std::string foreign_origin = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1.attacker.example\r\nContent-Length: 7\r\n\r\n" + body7;
    assert(setupResult(foreign_origin) == SetupHttpResult::PolicyRejected);
    const std::string chunked = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nTransfer-Encoding: chunked\r\n\r\n";
    assert(setupResult(chunked) == SetupHttpResult::PolicyRejected);

    // Android may close its write side after enqueueing a split request. Any
    // buffered body must still be drained after connected() becomes false.
    assert(setupHttpCanRead(false, 464U));
    assert(!setupHttpCanRead(false, 0U));

    std::string split_header = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 464\r\nX-Android-Filler: ";
    split_header += std::string(555U - split_header.size() - 4U, 'a');
    split_header += "\r\n\r\n";
    assert(split_header.size() == 555U);
    const std::string split_body(464U, 'x');
    assert(setupHttpParse(split_header.data(), split_header.size(), 2048U, 3072U, setup_request) == SetupHttpResult::NeedMore);
    const std::string complete_setup_request = split_header + split_body;
    assert(setupHttpParse(complete_setup_request.data(), complete_setup_request.size(), 2048U, 3072U, setup_request) == SetupHttpResult::Complete);
    assert(setup_request.header_end == 555U && setup_request.content_length == 464U && setup_request.total_length == 1019U);
    assert(setupHttpRoute(setup_request) == SetupHttpRoute::Save);
    assert(setupResult(complete_setup_request + "x") == SetupHttpResult::Malformed);

    std::string maximum_header = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Length: 1\r\nX-Fill: ";
    maximum_header += std::string(2048U - maximum_header.size() - 4U, 'z');
    maximum_header += "\r\n\r\nx";
    assert(setupResult(maximum_header) == SetupHttpResult::Complete);
    const std::string oversized_header(2049U, 'h');
    assert(setupResult(oversized_header) == SetupHttpResult::HeaderTooLarge);
    const std::string maximum_body_header = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Length: 3072\r\n\r\n";
    assert(setupResult(maximum_body_header + std::string(3072U, 'x')) == SetupHttpResult::Complete);
    const std::string oversized_body = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Length: 3073\r\n\r\n";
    assert(setupResult(oversized_body) == SetupHttpResult::BodyTooLarge);

    const std::string android_probe = "GET /generate_204 HTTP/1.1\r\nHost: connectivitycheck.gstatic.com\r\nConnection: close\r\n\r\n";
    assert(setupHttpParse(android_probe.data(), android_probe.size(), 2048U, 3072U, setup_request) == SetupHttpResult::Complete);
    assert(setupHttpRoute(setup_request) == SetupHttpRoute::CaptiveRedirect);
    const std::string setup_page = "GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
    assert(setupResult(setup_page) == SetupHttpResult::Complete);
    assert(setupHttpRoute(setup_request) == SetupHttpRoute::SetupPage);
    return 0;
}
