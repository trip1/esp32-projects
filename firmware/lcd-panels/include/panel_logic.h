#pragma once

#include <cstddef>
#include <cstdint>

enum class PanelKind : std::uint8_t { Mqtt = 1, Unifi = 2, Space = 3, Weather = 4 };

struct PanelConfig {
    char wifi_ssid[33]{};
    char wifi_password[64]{};
    char mqtt_host[65]{};
    std::uint16_t mqtt_port = 0U;
    char mqtt_username[65]{};
    char mqtt_password[65]{};
    char mqtt_topic[129]{};
    char label[17]{};
    char unifi_url[161]{};
    char latitude[17]{};
    char longitude[17]{};
};

void formatMqttText(const char* label, const char* value, char top[17], char bottom[17]);
void formatWeather(float temperature_c, int humidity_percent, const char* condition, char top[17], char bottom[17]);
void formatUnifi(bool wan_up, int latency_ms, int clients, int access_points, char top[17], char bottom[17]);
void formatSatellite(double latitude, double longitude, int altitude_km, char top[17], char bottom[17]);
bool validExactMqttTopic(const char* value);
bool validLatitude(const char* value);
bool validLongitude(const char* value);
bool mqttMessageStale(std::uint32_t now, std::uint32_t last_message_ms, bool message_received);
bool buildWeatherPath(double latitude, double longitude, char* output, std::size_t capacity);
const char* weatherCondition(int code);
bool panelConfigValid(PanelKind kind, const PanelConfig& value);
std::uint32_t panelCrc32(const unsigned char* data, std::size_t length);
