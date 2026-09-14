#include "panel_logic.h"

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

    char method[8]{};
    char target[64]{};
    std::size_t content_length = 0U;
    bool has_content_length = false;
    const std::string request = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Length: 7\r\n\r\n";
    assert(panelParseHttpRequest(request.data(), request.size(), 1024U, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    assert(std::strcmp(method, "POST") == 0 && std::strcmp(target, "/save") == 0 && content_length == 7U);
    const std::string hostile = "POST /save HTTP/1.1\r\nHost: attacker.example\r\nContent-Length: 7\r\n\r\n";
    assert(!panelParseHttpRequest(hostile.data(), hostile.size(), 1024U, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    const std::string missing_origin = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 7\r\n\r\n";
    assert(!panelParseHttpRequest(missing_origin.data(), missing_origin.size(), 1024U, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    const std::string duplicate = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 7\r\nContent-Length: 7\r\n\r\n";
    assert(!panelParseHttpRequest(duplicate.data(), duplicate.size(), 1024U, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    return 0;
}
