#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>

#include <cmath>
#include <cstring>

#include "panel_ca.h"
#include "bounded_http.h"
#include "bounded_mqtt.h"
#include "panel_config.h"
#include "panel_lcd.h"
#include "panel_logic.h"

#ifndef PANEL_KIND
#error "PANEL_KIND is required"
#endif

namespace {
constexpr uint32_t kWifiRetryMs = 30000U;
constexpr uint32_t kLcdRetryMs = 5000U;
constexpr uint32_t kHttpDeadlineMs = 8000U;
constexpr size_t kMaximumResponse = 2048U;

PanelConfig active_config{};
bool active_ready = false;
PanelLcd1602 lcd;
bool lcd_ready = false;
uint32_t last_lcd_attempt_ms = 0U;
uint32_t last_wifi_attempt_ms = 0U;
uint32_t last_refresh_ms = 0U;
uint32_t last_mqtt_attempt_ms = 0U;
uint32_t last_mqtt_message_ms = 0U;
bool mqtt_message_received = false;
char current_top[17] = "LCD Panel       ";
char current_bottom[17] = "Starting...     ";
BoundedMqttClient mqtt;

PanelKind kind() { return static_cast<PanelKind>(PANEL_KIND); }

struct FetchArguments {
    const char* url;
    char* output;
    size_t capacity;
    QueueHandle_t completed;
};

void fetchTask(void* raw) {
    auto* arguments = static_cast<FetchArguments*>(raw);
    const bool success = panelHttpGetBounded(arguments->url, PANEL_ISRG_ROOT_X1, arguments->output, arguments->capacity);
    xQueueSend(arguments->completed, &success, 0U);
    vTaskSuspend(nullptr);
}

bool fetchUrlBounded(const char* url, char* output, size_t capacity) {
    QueueHandle_t completed = xQueueCreate(1U, sizeof(bool));
    if (completed == nullptr) return false;
    FetchArguments arguments{url, output, capacity, completed};
    TaskHandle_t task = nullptr;
    if (xTaskCreate(fetchTask, "panel-http", 10240U, &arguments, 1U, &task) != pdPASS) {
        vQueueDelete(completed); return false;
    }
    bool success = false;
    if (xQueueReceive(completed, &success, pdMS_TO_TICKS(kHttpDeadlineMs)) != pdTRUE) {
        vTaskDelete(task);
        vQueueDelete(completed);
        Serial.println("HTTP operation exceeded eight-second deadline; restarting to reset transport state");
        delay(50); ESP.restart();
        return false;
    }
    vTaskDelete(task);
    vQueueDelete(completed);
    return success;
}

bool connectWifi(const PanelConfig& value, uint32_t timeout_ms) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(value.wifi_ssid, value.wifi_password);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && static_cast<uint32_t>(millis() - started) < timeout_ms) delay(100);
    return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0U, 0U, 0U, 0U);
}

bool ensureUtcTime() {
    if (time(nullptr) >= 1704067200) return true;
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    const uint32_t started = millis();
    while (time(nullptr) < 1704067200 && static_cast<uint32_t>(millis() - started) < 10000U) delay(100);
    return time(nullptr) >= 1704067200;
}

bool timestampFresh(long long timestamp, long long tolerance_seconds) {
    const long long now = static_cast<long long>(time(nullptr));
    if (timestamp < 1704067200LL || timestamp > 4102444800LL || now < 1704067200LL || now > 4102444800LL) return false;
    const long long difference = now >= timestamp ? now - timestamp : timestamp - now;
    return difference <= tolerance_seconds;
}

bool fetchWeather(const PanelConfig& value, char top[17], char bottom[17]) {
    if (!ensureUtcTime()) return false;
    char path[192]{};
    if (!buildWeatherPath(std::strtod(value.latitude, nullptr), std::strtod(value.longitude, nullptr), path, sizeof(path))) return false;
    char url[256]{};
    const int length = std::snprintf(url, sizeof(url), "https://api.open-meteo.com%s", path);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(url)) return false;
    char body[kMaximumResponse + 1U]{};
    if (!fetchUrlBounded(url, body, sizeof(body))) return false;
    JsonDocument document;
    if (deserializeJson(document, body) != DeserializationError::Ok) return false;
    JsonVariant current = document["current"];
    if (!current.is<JsonObject>()) return false;
    const float temperature = current["temperature_2m"] | NAN;
    const int humidity = current["relative_humidity_2m"] | -1;
    const int code = current["weather_code"] | -1;
    const long long timestamp = current["time"] | 0LL;
    if (!std::isfinite(temperature) || humidity < 0 || humidity > 100 || code < 0 || !timestampFresh(timestamp, 3600LL)) return false;
    formatWeather(temperature, humidity, weatherCondition(code), top, bottom);
    return true;
}

bool fetchSatellite(char top[17], char bottom[17]) {
    if (!ensureUtcTime()) return false;
    char body[kMaximumResponse + 1U]{};
    if (!fetchUrlBounded("https://api.wheretheiss.at/v1/satellites/25544", body, sizeof(body))) return false;
    JsonDocument document;
    if (deserializeJson(document, body) != DeserializationError::Ok || (document["id"] | 0) != 25544) return false;
    const double latitude = document["latitude"] | NAN;
    const double longitude = document["longitude"] | NAN;
    const double altitude = document["altitude"] | NAN;
    const long long timestamp = document["timestamp"] | 0LL;
    if (!std::isfinite(latitude) || latitude < -90.0 || latitude > 90.0
        || !std::isfinite(longitude) || longitude < -180.0 || longitude > 180.0
        || !std::isfinite(altitude) || altitude < 100.0 || altitude > 1000.0 || !timestampFresh(timestamp, 120LL)) return false;
    formatSatellite(latitude, longitude, static_cast<int>(std::lround(altitude)), top, bottom);
    return true;
}

bool fetchUnifi(const PanelConfig& value, char top[17], char bottom[17]) {
    if (!ensureUtcTime()) return false;
    char body[kMaximumResponse + 1U]{};
    if (!fetchUrlBounded(value.unifi_url, body, sizeof(body))) return false;
    JsonDocument document;
    if (deserializeJson(document, body) != DeserializationError::Ok) return false;
    JsonObject root = document.as<JsonObject>();
    if (root.isNull() || root.size() != 5U || !root["wan_up"].is<bool>() || !root["latency_ms"].is<int>()
        || !root["clients"].is<int>() || !root["access_points"].is<int>() || !root["observed_at"].is<long long>()) return false;
    const bool wan_up = document["wan_up"].as<bool>();
    const int latency = document["latency_ms"].as<int>();
    const int clients = document["clients"].as<int>();
    const int access_points = document["access_points"].as<int>();
    const long long observed_at = document["observed_at"].as<long long>();
    if (latency < 0 || latency > 60000 || clients < 0 || clients > 100000 || access_points < 0 || access_points > 10000
        || !timestampFresh(observed_at, 120LL)) return false;
    formatUnifi(wan_up, latency, clients, access_points, top, bottom);
    return true;
}

void mqttCallback(const uint8_t* payload, size_t length) {
    if (length == 0U || length > 256U) return;
    char raw[257]{};
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char value = payload[index];
        raw[index] = value >= 0x20U && value <= 0x7eU ? static_cast<char>(value) : ' ';
    }
    char value[64]{};
    JsonDocument document;
    if (deserializeJson(document, payload, length) == DeserializationError::Ok
        && document["temperature_c"].is<float>() && document["humidity_percent"].is<float>()) {
        std::snprintf(value, sizeof(value), "%.1f C / %.0f%%", document["temperature_c"].as<double>(), document["humidity_percent"].as<double>());
    } else if (deserializeJson(document, payload, length) == DeserializationError::Ok && document["value"].is<const char*>()) {
        std::snprintf(value, sizeof(value), "%s", document["value"].as<const char*>());
    } else {
        std::snprintf(value, sizeof(value), "%s", raw);
    }
    formatMqttText(active_config.label, value, current_top, current_bottom);
    last_mqtt_message_ms = millis();
    mqtt_message_received = true;
}

bool validateCandidate(const PanelConfig& value) {
    if (!connectWifi(value, 15000U)) return false;
    char top[17]{}; char bottom[17]{};
    if (kind() == PanelKind::Weather) return fetchWeather(value, top, bottom);
    if (kind() == PanelKind::Space) return fetchSatellite(top, bottom);
    if (kind() == PanelKind::Unifi) return fetchUnifi(value, top, bottom);
    BoundedMqttClient client;
    const bool connected = client.connect(value, mqttCallback);
    client.stop();
    return connected;
}

void showCurrent() {
    const uint32_t now = millis();
    if (!lcd_ready && static_cast<uint32_t>(now - last_lcd_attempt_ms) >= kLcdRetryMs) {
        last_lcd_attempt_ms = now; lcd_ready = lcd.begin();
    }
    if (lcd_ready && !lcd.show(current_top, current_bottom)) lcd_ready = false;
}

void setUnavailable() {
    if (kind() == PanelKind::Mqtt) formatMqttText("MQTT Home", "Waiting for data", current_top, current_bottom);
    else if (kind() == PanelKind::Unifi) formatUnifi(false, -1, -1, -1, current_top, current_bottom);
    else if (kind() == PanelKind::Weather) formatWeather(NAN, -1, nullptr, current_top, current_bottom);
    else formatSatellite(NAN, NAN, -1, current_top, current_bottom);
}

void refreshPanel() {
    char top[17]{}; char bottom[17]{};
    bool success = false;
    if (kind() == PanelKind::Weather) success = fetchWeather(active_config, top, bottom);
    else if (kind() == PanelKind::Space) success = fetchSatellite(top, bottom);
    else if (kind() == PanelKind::Unifi) success = fetchUnifi(active_config, top, bottom);
    if (success) { std::memcpy(current_top, top, sizeof(top)); std::memcpy(current_bottom, bottom, sizeof(bottom)); }
    else setUnavailable();
    showCurrent();
}
}

void setup() {
    Serial.begin(115200);
    lcd_ready = lcd.begin();
    setUnavailable(); showCurrent();
    pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
    active_ready = panelLoadConfig("active", active_config);
    PanelConfig pending{};
    const bool pending_valid = panelLoadConfig("pending", pending);
    const bool pending_matches_active = pending_valid && active_ready && std::memcmp(&pending, &active_config, sizeof(pending)) == 0;
    const bool pending_ready = pending_valid && !pending_matches_active && !panelPendingSuppressed();
    if (!pending_valid) panelRemoveConfig("pending");
    else if (pending_matches_active) panelRemoveConfig("pending");
    if ((active_ready || pending_ready) && panelRecoveryRequested()) {
        if (!panelStartProvisioning()) { delay(30000); ESP.restart(); }
        return;
    }
    if (pending_ready) {
        panelBeginPendingValidation();
        if (validateCandidate(pending) && panelPromotePending(pending)) {
            active_config = pending; active_ready = true;
            Serial.println("Pending panel settings verified and promoted");
        } else {
            panelRejectPending(); WiFi.disconnect(true, false);
            Serial.println("Pending panel settings rejected; active settings preserved");
        }
    }
    if (!active_ready) {
        if (!panelStartProvisioning()) { delay(30000); ESP.restart(); }
        return;
    }
    if (WiFi.status() != WL_CONNECTED) connectWifi(active_config, 15000U);
    if (kind() == PanelKind::Mqtt) mqtt.connect(active_config, mqttCallback);
    else refreshPanel();
}

void loop() {
    if (panelProvisioningActive()) { panelHandleProvisioning(); delay(2); return; }
    const uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED && static_cast<uint32_t>(now - last_wifi_attempt_ms) >= kWifiRetryMs) {
        last_wifi_attempt_ms = now; WiFi.begin(active_config.wifi_ssid, active_config.wifi_password);
    }
    if (kind() == PanelKind::Mqtt) {
        if (WiFi.status() == WL_CONNECTED && !mqtt.connected() && static_cast<uint32_t>(now - last_mqtt_attempt_ms) >= 10000U) {
            last_mqtt_attempt_ms = now; mqtt.connect(active_config, mqttCallback);
        }
        if (mqtt.connected()) mqtt.loop();
        const uint32_t mqtt_now = millis();
        if (mqttMessageStale(mqtt_now, last_mqtt_message_ms, mqtt_message_received)) {
            formatMqttText(active_config.label, "Data stale", current_top, current_bottom); mqtt_message_received = false;
        }
        if (static_cast<uint32_t>(now - last_refresh_ms) >= 1000U) { last_refresh_ms = now; showCurrent(); }
    } else {
        const uint32_t interval = kind() == PanelKind::Weather ? 600000U : kind() == PanelKind::Space ? 10000U : 30000U;
        if (WiFi.status() == WL_CONNECTED && static_cast<uint32_t>(now - last_refresh_ms) >= interval) {
            last_refresh_ms = now; refreshPanel();
        }
        if (!lcd_ready && static_cast<uint32_t>(now - last_lcd_attempt_ms) >= kLcdRetryMs) showCurrent();
    }
    delay(5);
}
