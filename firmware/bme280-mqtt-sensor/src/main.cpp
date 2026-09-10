#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>

#include "sensor_config_logic.h"

#ifndef SENSOR_SDA_PIN
#error "SENSOR_SDA_PIN must be defined by the board environment"
#endif
#ifndef SENSOR_SCL_PIN
#error "SENSOR_SCL_PIN must be defined by the board environment"
#endif
#ifndef SETUP_BUTTON_PIN
#error "SETUP_BUTTON_PIN must be defined by the board environment"
#endif

namespace {
constexpr uint32_t kConfigMagic = 0x424d4532U;
constexpr uint16_t kConfigVersion = 1U;
constexpr uint32_t kWifiTimeoutMs = 15000U;
constexpr uint32_t kProvisioningTimeoutMs = 10U * 60U * 1000U;
constexpr uint32_t kRecoveryWindowMs = 5000U;
constexpr uint32_t kRecoveryHoldMs = 2000U;
constexpr uint32_t kFallbackSleepMinutes = 5U;
constexpr size_t kMaximumRequestBytes = 2048U;
constexpr size_t kMaximumHeaderBytes = 1024U;
constexpr size_t kMaximumBodyBytes = 1024U;
constexpr uint32_t kRejectedPendingMarker = 0x52504e44U;
RTC_DATA_ATTR uint32_t rejected_pending_marker = 0U;

struct StoredConfig {
    uint32_t magic;
    uint16_t version;
    uint16_t mqtt_port;
    uint32_t sleep_minutes;
    char wifi_ssid[33];
    char wifi_password[64];
    char mqtt_host[129];
    char mqtt_username[65];
    char mqtt_password[129];
    char topic_prefix[97];
    uint32_t crc32;
};
static_assert(sizeof(StoredConfig) < 1024U, "configuration should remain a small NVS value");

struct Reading {
    bool valid = false;
    uint8_t address = 0;
    float temperature_c = NAN;
    float humidity_percent = NAN;
    float pressure_hpa = NAN;
};

struct FormFields {
    char csrf[17]{};
    char wifi_ssid[33]{};
    char wifi_password[64]{};
    char mqtt_host[129]{};
    char mqtt_port[6]{};
    char mqtt_username[65]{};
    char mqtt_password[129]{};
    char topic_prefix[97]{};
    char sleep_minutes[5]{};
    uint16_t seen = 0;
};

constexpr uint16_t kFieldCsrf = 1U << 0U;
constexpr uint16_t kFieldSsid = 1U << 1U;
constexpr uint16_t kFieldWifiPassword = 1U << 2U;
constexpr uint16_t kFieldMqttHost = 1U << 3U;
constexpr uint16_t kFieldMqttPort = 1U << 4U;
constexpr uint16_t kFieldMqttUsername = 1U << 5U;
constexpr uint16_t kFieldMqttPassword = 1U << 6U;
constexpr uint16_t kFieldTopicPrefix = 1U << 7U;
constexpr uint16_t kFieldSleepMinutes = 1U << 8U;
constexpr uint16_t kRequiredFields = (1U << 9U) - 1U;

StoredConfig active_config{};
bool active_config_ready = false;
DNSServer dns;
WiFiServer provisioning_server(80);
WiFiClient network;
PubSubClient mqtt(network);
bool provisioning = false;
uint32_t provisioning_started_ms = 0;
char csrf_token[17]{};
bool restart_requested = false;

bool isTerminated(const char* value, size_t capacity) {
    return std::memchr(value, '\0', capacity) != nullptr;
}

uint32_t configChecksum(const StoredConfig& value) {
    return configurationCrc32(reinterpret_cast<const uint8_t*>(&value), offsetof(StoredConfig, crc32));
}

bool configIsValid(const StoredConfig& value) {
    if (value.magic != kConfigMagic || value.version != kConfigVersion || value.crc32 != configChecksum(value)) return false;
    if (!isTerminated(value.wifi_ssid, sizeof(value.wifi_ssid)) ||
        !isTerminated(value.wifi_password, sizeof(value.wifi_password)) ||
        !isTerminated(value.mqtt_host, sizeof(value.mqtt_host)) ||
        !isTerminated(value.mqtt_username, sizeof(value.mqtt_username)) ||
        !isTerminated(value.mqtt_password, sizeof(value.mqtt_password)) ||
        !isTerminated(value.topic_prefix, sizeof(value.topic_prefix))) return false;
    return isValidWifiSsid(value.wifi_ssid) &&
           isValidWifiPassword(value.wifi_password) &&
           isValidMqttHost(value.mqtt_host) &&
           isValidOptionalCredential(value.mqtt_username, sizeof(value.mqtt_username) - 1U) &&
           isValidOptionalCredential(value.mqtt_password, sizeof(value.mqtt_password) - 1U) &&
           !(value.mqtt_username[0] == '\0' && value.mqtt_password[0] != '\0') &&
           isValidTopicPrefix(value.topic_prefix) &&
           value.mqtt_port > 0U &&
           isValidSleepMinutes(value.sleep_minutes);
}

bool loadRecord(const char* key, StoredConfig& value) {
    Preferences preferences;
    if (!preferences.begin("bme280mqtt", true)) return false;
    const size_t length = preferences.getBytesLength(key);
    const size_t read = length == sizeof(value) ? preferences.getBytes(key, &value, sizeof(value)) : 0U;
    preferences.end();
    return read == sizeof(value) && configIsValid(value);
}

bool storeRecord(const char* key, const StoredConfig& value) {
    Preferences preferences;
    if (!preferences.begin("bme280mqtt", false)) return false;
    const bool stored = preferences.putBytes(key, &value, sizeof(value)) == sizeof(value);
    preferences.end();
    if (!stored) return false;
    StoredConfig verified{};
    return loadRecord(key, verified) && std::memcmp(&verified, &value, sizeof(value)) == 0;
}

bool removeRecord(const char* key) {
    Preferences preferences;
    if (!preferences.begin("bme280mqtt", false)) return false;
    const bool removed = !preferences.isKey(key) || preferences.remove(key);
    preferences.end();
    return removed;
}

bool clearConfiguration() {
    Preferences preferences;
    if (!preferences.begin("bme280mqtt", false)) return false;
    const bool cleared = preferences.clear();
    preferences.end();
    return cleared;
}

bool promotePending(const StoredConfig& pending) {
    if (!storeRecord("active", pending)) return false;
    return removeRecord("pending");
}

template <size_t N>
bool copyText(const char* source, size_t length, char (&destination)[N]) {
    if (length >= N) return false;
    std::memcpy(destination, source, length);
    destination[length] = '\0';
    return true;
}

bool parseUnsigned(const char* value, uint32_t maximum, uint32_t& output) {
    if (value == nullptr || *value == '\0') return false;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed > maximum) return false;
    output = static_cast<uint32_t>(parsed);
    return true;
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

template <size_t N>
bool decodeFormValue(const char* begin, const char* end, char (&output)[N]) {
    size_t used = 0;
    while (begin < end) {
        char value = *begin++;
        if (value == '+') value = ' ';
        else if (value == '%') {
            if (end - begin < 2) return false;
            const int high = hexValue(begin[0]);
            const int low = hexValue(begin[1]);
            if (high < 0 || low < 0) return false;
            value = static_cast<char>((high << 4) | low);
            begin += 2;
        }
        if (value == '\0' || used + 1U >= N) return false;
        output[used++] = value;
    }
    output[used] = '\0';
    return true;
}

bool assignFormField(const char* key_begin, const char* key_end, const char* value_begin, const char* value_end, FormFields& fields) {
    const std::string key(key_begin, key_end);
    uint16_t bit = 0;
    bool decoded = false;
    if (key == "csrf") { bit = kFieldCsrf; decoded = decodeFormValue(value_begin, value_end, fields.csrf); }
    else if (key == "ssid") { bit = kFieldSsid; decoded = decodeFormValue(value_begin, value_end, fields.wifi_ssid); }
    else if (key == "wifi_password") { bit = kFieldWifiPassword; decoded = decodeFormValue(value_begin, value_end, fields.wifi_password); }
    else if (key == "mqtt_host") { bit = kFieldMqttHost; decoded = decodeFormValue(value_begin, value_end, fields.mqtt_host); }
    else if (key == "mqtt_port") { bit = kFieldMqttPort; decoded = decodeFormValue(value_begin, value_end, fields.mqtt_port); }
    else if (key == "mqtt_username") { bit = kFieldMqttUsername; decoded = decodeFormValue(value_begin, value_end, fields.mqtt_username); }
    else if (key == "mqtt_password") { bit = kFieldMqttPassword; decoded = decodeFormValue(value_begin, value_end, fields.mqtt_password); }
    else if (key == "topic_prefix") { bit = kFieldTopicPrefix; decoded = decodeFormValue(value_begin, value_end, fields.topic_prefix); }
    else if (key == "sleep_minutes") { bit = kFieldSleepMinutes; decoded = decodeFormValue(value_begin, value_end, fields.sleep_minutes); }
    else return false;
    if (!decoded || (fields.seen & bit) != 0U) return false;
    fields.seen |= bit;
    return true;
}

bool parseForm(const char* body, size_t length, FormFields& fields) {
    const char* cursor = body;
    const char* end = body + length;
    unsigned count = 0;
    while (cursor < end && count++ < 9U) {
        const char* pair_end = static_cast<const char*>(std::memchr(cursor, '&', end - cursor));
        if (pair_end == nullptr) pair_end = end;
        const char* equals = static_cast<const char*>(std::memchr(cursor, '=', pair_end - cursor));
        if (equals == nullptr || !assignFormField(cursor, equals, equals + 1, pair_end, fields)) return false;
        cursor = pair_end < end ? pair_end + 1 : end;
    }
    return cursor == end && fields.seen == kRequiredFields;
}

bool validCsrfBody(const char* body, size_t length) {
    static constexpr char prefix[] = "csrf=";
    if (length <= sizeof(prefix) - 1U || std::memcmp(body, prefix, sizeof(prefix) - 1U) != 0) return false;
    char decoded[17]{};
    return decodeFormValue(body + sizeof(prefix) - 1U, body + length, decoded) &&
           std::strcmp(decoded, csrf_token) == 0;
}

String setupPage(const String& message = String()) {
    String page;
    page.reserve(4200);
    page += F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>BME280 Setup</title><style>body{margin:0;background:#0b1014;color:#eef5f0;font:16px/1.5 system-ui,sans-serif}main{max-width:680px;margin:auto;padding:28px 18px}h1{font-size:42px;line-height:1}label{display:grid;gap:5px;margin:14px 0;color:#b7c3be}input{min-height:46px;padding:0 12px;border:1px solid #3b4a51;border-radius:5px;background:#172127;color:#fff;font:inherit}button{min-height:46px;padding:0 16px;border:0;border-radius:5px;background:#a7f46a;color:#10200a;font-weight:700}.warn,.message{padding:14px;border:1px solid #765f38;background:#211c13}.message{border-color:#a7f46a}.muted{color:#9caaa5}code{color:#a7f46a}</style></head><body><main><p class=muted>ESP32 · PROTECTED PROVISIONING</p><h1>BME280 MQTT Sensor</h1>");
    if (!message.isEmpty()) page += "<p class=message>" + message + "</p>";
    page += F("<p class=warn>This temporary setup network uses the random password printed on USB serial. Wi-Fi and MQTT passwords are stored in ESP32 NVS and are not encrypted at rest.</p><form method=post action=/save><input type=hidden name=csrf value='");
    page += csrf_token;
    page += F("'><label>Wi-Fi SSID<input name=ssid maxlength=32 required autocomplete=off></label><label>Wi-Fi password<input name=wifi_password type=password maxlength=63 autocomplete=new-password></label><label>MQTT host or IP<input name=mqtt_host maxlength=128 required placeholder=192.168.1.10></label><label>MQTT port<input name=mqtt_port type=number min=1 max=65535 value=1883 required></label><label>MQTT username<input name=mqtt_username maxlength=64 autocomplete=off></label><label>MQTT password<input name=mqtt_password type=password maxlength=128 autocomplete=new-password></label><label>Topic prefix<input name=topic_prefix maxlength=96 value=home/environment required></label><label>Wake interval (minutes)<input name=sleep_minutes type=number min=1 max=1440 value=5 required></label><button type=submit>Save pending configuration</button></form><p class=muted>The sensor tests pending Wi-Fi and MQTT settings after restart. It promotes them only after a successful publish; the previous active configuration is preserved on failure.</p><form method=post action=/clear><input type=hidden name=csrf value='");
    page += csrf_token;
    page += F("'><button type=submit>Erase saved configuration</button></form></main></body></html>");
    return page;
}

void sendHttp(WiFiClient& client, int status, const char* reason, const String& body) {
    client.printf("HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",
                  status, reason, static_cast<unsigned>(body.length()));
    client.print(body);
}

bool createCandidate(const FormFields& fields, StoredConfig& candidate) {
    uint32_t port = 0;
    uint32_t sleep_minutes = 0;
    if (!parseUnsigned(fields.mqtt_port, 65535U, port) || !parseUnsigned(fields.sleep_minutes, 1440U, sleep_minutes)) return false;
    candidate = {};
    candidate.magic = kConfigMagic;
    candidate.version = kConfigVersion;
    candidate.mqtt_port = static_cast<uint16_t>(port);
    candidate.sleep_minutes = sleep_minutes;
    if (!copyText(fields.wifi_ssid, std::strlen(fields.wifi_ssid), candidate.wifi_ssid) ||
        !copyText(fields.wifi_password, std::strlen(fields.wifi_password), candidate.wifi_password) ||
        !copyText(fields.mqtt_host, std::strlen(fields.mqtt_host), candidate.mqtt_host) ||
        !copyText(fields.mqtt_username, std::strlen(fields.mqtt_username), candidate.mqtt_username) ||
        !copyText(fields.mqtt_password, std::strlen(fields.mqtt_password), candidate.mqtt_password) ||
        !copyText(fields.topic_prefix, std::strlen(fields.topic_prefix), candidate.topic_prefix)) return false;
    candidate.crc32 = configChecksum(candidate);
    return configIsValid(candidate);
}

void handlePost(WiFiClient& client, const char* path, const char* body, size_t body_length) {
    if (std::strcmp(path, "/clear") == 0) {
        if (!validCsrfBody(body, body_length)) {
            sendHttp(client, 400, "Bad Request", setupPage("Request rejected."));
        } else if (!clearConfiguration()) {
            sendHttp(client, 503, "Unavailable", setupPage("NVS clear failed."));
        } else {
            sendHttp(client, 200, "OK", setupPage("Configuration erased. Restarting into setup."));
            active_config_ready = false;
            rejected_pending_marker = 0U;
            restart_requested = true;
        }
        return;
    }
    FormFields fields{};
    if (!parseForm(body, body_length, fields) || std::strcmp(fields.csrf, csrf_token) != 0) {
        sendHttp(client, 400, "Bad Request", setupPage("Request rejected."));
        return;
    }
    if (std::strcmp(path, "/save") != 0) {
        sendHttp(client, 404, "Not Found", setupPage());
        return;
    }
    StoredConfig candidate{};
    if (!createCandidate(fields, candidate)) {
        sendHttp(client, 400, "Bad Request", setupPage("Check the SSID, password lengths, MQTT host, topic prefix, and wake interval."));
        return;
    }
    if (!storeRecord("pending", candidate)) {
        sendHttp(client, 503, "Unavailable", setupPage("NVS write or verification failed; active settings were preserved."));
        return;
    }
    sendHttp(client, 200, "OK", setupPage("Pending settings saved. Restarting to test them."));
    rejected_pending_marker = 0U;
    restart_requested = true;
}

bool readHttpRequest(WiFiClient& client, char* request, size_t capacity, size_t& used, size_t& header_end,
                     size_t& content_length, char* method, size_t method_capacity, char* path, size_t path_capacity) {
    const uint32_t started = millis();
    header_end = 0;
    content_length = 0;
    while (client.connected() && static_cast<uint32_t>(millis() - started) < 2000U) {
        while (client.available()) {
            if (used + 1U >= capacity) return false;
            request[used++] = static_cast<char>(client.read());
            request[used] = '\0';
            if (header_end == 0U && used >= 4U && std::memcmp(request + used - 4U, "\r\n\r\n", 4U) == 0) {
                header_end = used;
                if (header_end > kMaximumHeaderBytes) return false;
                bool has_content_length = false;
                if (!parseBoundedHttpRequest(request, header_end, kMaximumBodyBytes,
                        method, method_capacity, path, path_capacity, content_length, has_content_length)) return false;
            }
            if (header_end != 0U && used >= header_end + content_length) return true;
        }
        delay(1);
    }
    return header_end != 0U && used >= header_end + content_length;
}

void handleProvisioningClient() {
    WiFiClient client = provisioning_server.accept();
    if (!client) return;
    char request[kMaximumRequestBytes + 1U]{};
    size_t used = 0;
    size_t header_end = 0;
    size_t content_length = 0;
    char method[8]{};
    char path[64]{};
    if (!readHttpRequest(client, request, sizeof(request), used, header_end, content_length,
                         method, sizeof(method), path, sizeof(path))) {
        sendHttp(client, 413, "Payload Too Large", setupPage("Request too large or incomplete."));
        client.stop();
        return;
    }
    if (std::strcmp(method, "GET") == 0) {
        sendHttp(client, 200, "OK", setupPage());
    } else if (std::strcmp(method, "POST") == 0 && content_length > 0U) {
        handlePost(client, path, request + header_end, content_length);
    } else {
        sendHttp(client, 405, "Method Not Allowed", setupPage());
    }
    delay(1);
    client.stop();
}

void makeRandomHex(char* output, size_t bytes) {
    static constexpr char hex[] = "0123456789abcdef";
    for (size_t index = 0; index < bytes; ++index) {
        const uint8_t value = static_cast<uint8_t>(esp_random());
        output[index * 2U] = hex[value >> 4U];
        output[index * 2U + 1U] = hex[value & 0x0fU];
    }
    output[bytes * 2U] = '\0';
}

void startProvisioning() {
    uint8_t mac[6]{};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char ssid[32];
    char password[17];
    std::snprintf(ssid, sizeof(ssid), "BME280-Setup-%02X%02X%02X", mac[3], mac[4], mac[5]);
    makeRandomHex(password, 8U);
    makeRandomHex(csrf_token, 8U);
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid, password, 1, false, 1)) {
        Serial.println("Provisioning AP failed; entering fail-safe sleep");
        return;
    }
    dns.start(53, "*", WiFi.softAPIP());
    provisioning_server.begin();
    provisioning = true;
    provisioning_started_ms = millis();
    Serial.printf("Provisioning: join %s with password %s, then open http://%s\n",
                  ssid, password, WiFi.softAPIP().toString().c_str());
}

Reading readSensor() {
    Reading reading;
    if (!Wire.begin(SENSOR_SDA_PIN, SENSOR_SCL_PIN, 100000U)) {
        Serial.println("I2C initialization failed");
        return reading;
    }
    Adafruit_BME280 sensor;
    if (sensor.begin(0x76, &Wire)) reading.address = 0x76;
    else if (sensor.begin(0x77, &Wire)) reading.address = 0x77;
    else {
        Serial.println("BME280 not found at 0x76 or 0x77");
        Wire.end();
        return reading;
    }
    sensor.setSampling(Adafruit_BME280::MODE_FORCED, Adafruit_BME280::SAMPLING_X1,
                       Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
                       Adafruit_BME280::FILTER_OFF, Adafruit_BME280::STANDBY_MS_0_5);
    if (sensor.takeForcedMeasurement()) {
        reading.temperature_c = sensor.readTemperature();
        reading.humidity_percent = sensor.readHumidity();
        reading.pressure_hpa = sensor.readPressure() / 100.0F;
        reading.valid = isValidBme280Reading(reading.temperature_c, reading.humidity_percent, reading.pressure_hpa);
    }
    Wire.end();
    return reading;
}

bool connectWifi(const StoredConfig& value) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(value.wifi_ssid, value.wifi_password);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && static_cast<uint32_t>(millis() - started) < kWifiTimeoutMs) delay(100);
    return WiFi.status() == WL_CONNECTED;
}

const char* chipPrefix() {
#if defined(CONFIG_IDF_TARGET_ESP32C6)
    return "esp32c6";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return "esp32c3";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    return "esp32s3";
#else
    return "esp32";
#endif
}

bool publishReading(const Reading& reading, const StoredConfig& value) {
    uint8_t mac[6]{};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char device_id[32];
    std::snprintf(device_id, sizeof(device_id), "%s-bme280-%02x%02x%02x%02x%02x%02x", chipPrefix(), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    char topic[160];
    const int topic_length = std::snprintf(topic, sizeof(topic), "%s/%s/state", value.topic_prefix, device_id);
    if (topic_length <= 0 || static_cast<size_t>(topic_length) >= sizeof(topic)) return false;
    mqtt.setServer(value.mqtt_host, value.mqtt_port);
    if (!mqtt.setBufferSize(768)) return false;
    mqtt.setSocketTimeout(5);
    network.setConnectionTimeout(5000);
    network.setTimeout(5000);
    const bool connected = value.mqtt_username[0] == '\0' ? mqtt.connect(device_id) : mqtt.connect(device_id, value.mqtt_username, value.mqtt_password);
    if (!connected) return false;
    JsonDocument document;
    document["device_id"] = device_id;
    document["sensor"] = "BME280";
    document["valid"] = reading.valid;
    if (reading.valid) {
        document["temperature_c"] = std::round(reading.temperature_c * 100.0F) / 100.0F;
        document["humidity_percent"] = std::round(reading.humidity_percent * 100.0F) / 100.0F;
        document["pressure_hpa"] = std::round(reading.pressure_hpa * 100.0F) / 100.0F;
        char address[5];
        std::snprintf(address, sizeof(address), "0x%02X", reading.address);
        document["i2c_address"] = address;
    } else document["error"] = "bme280_read_failed";
    document["wifi_rssi_dbm"] = WiFi.RSSI();
    document["sleep_minutes"] = value.sleep_minutes;
    document["awake_ms"] = millis();
    char payload[512];
    const size_t length = serializeJson(document, payload, sizeof(payload));
    const bool published = length > 0U && length < sizeof(payload) && mqtt.publish(topic, payload, true);
    mqtt.disconnect();
    return published;
}

[[noreturn]] void sleepForMinutes(uint32_t minutes) {
    provisioning_server.stop();
    dns.stop();
    WiFi.disconnect(true, false);
    const uint64_t sleep_us = static_cast<uint64_t>(minutes) * 60ULL * 1000000ULL;
    if (esp_sleep_enable_timer_wakeup(sleep_us) != ESP_OK) {
        Serial.println("Timer wakeup failed; entering indefinite deep sleep for battery safety");
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    } else Serial.printf("Deep sleeping for %lu minute(s)\n", static_cast<unsigned long>(minutes));
    Serial.flush();
    while (true) {
        esp_deep_sleep_start();
        delay(1000);
    }
}

bool recoveryRequested() {
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP) return false;
    Serial.println("Hold BOOT now for two seconds to open setup");
    const uint32_t started = millis();
    uint32_t held_since = 0;
    while (static_cast<uint32_t>(millis() - started) < kRecoveryWindowMs) {
        if (digitalRead(SETUP_BUTTON_PIN) == LOW) {
            if (held_since == 0U) held_since = millis();
            if (static_cast<uint32_t>(millis() - held_since) >= kRecoveryHoldMs) return true;
        } else held_since = 0;
        delay(10);
    }
    return false;
}

void processConfiguredWake(const StoredConfig& value, bool pending) {
    const Reading reading = readSensor();
    const bool wifi_ready = connectWifi(value);
    const bool published = wifi_ready && publishReading(reading, value);
    bool promoted = !pending;
    if (pending) {
        if (published && promotePending(value)) {
            Serial.println("Pending configuration verified and promoted");
            active_config = value;
            active_config_ready = true;
            promoted = true;
        } else {
            Serial.println("Pending configuration failed; preserving active configuration");
            rejected_pending_marker = removeRecord("pending") ? 0U : kRejectedPendingMarker;
            if (rejected_pending_marker != 0U) Serial.println("Pending cleanup failed; ignoring it until replacement or cold boot");
        }
    }
    const uint32_t sleep_minutes = sleepMinutesAfterPendingAttempt(
        pending, published, promoted, active_config_ready, value.sleep_minutes,
        active_config.sleep_minutes, kFallbackSleepMinutes);
    sleepForMinutes(sleep_minutes);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
    active_config_ready = loadRecord("active", active_config);
    StoredConfig pending{};
    const bool pending_record_valid = loadRecord("pending", pending);
    const bool pending_ready = shouldProcessPending(
        pending_record_valid, rejected_pending_marker == kRejectedPendingMarker);
    if (!pending_record_valid) {
        if (removeRecord("pending")) rejected_pending_marker = 0U;
    }
    if ((active_config_ready || pending_ready) && recoveryRequested()) {
        startProvisioning();
        if (!provisioning) sleepForMinutes(active_config_ready ? active_config.sleep_minutes : kFallbackSleepMinutes);
        return;
    }
    if (pending_ready) processConfiguredWake(pending, true);
    if (!active_config_ready) {
        startProvisioning();
        if (!provisioning) sleepForMinutes(kFallbackSleepMinutes);
        return;
    }
    processConfiguredWake(active_config, false);
}

void loop() {
    if (!provisioning) {
        sleepForMinutes(active_config_ready ? active_config.sleep_minutes : kFallbackSleepMinutes);
    }
    dns.processNextRequest();
    handleProvisioningClient();
    if (restart_requested) {
        delay(250);
        ESP.restart();
    }
    if (static_cast<uint32_t>(millis() - provisioning_started_ms) >= kProvisioningTimeoutMs) {
        Serial.println("Provisioning timed out");
        sleepForMinutes(active_config_ready ? active_config.sleep_minutes : kFallbackSleepMinutes);
    }
    delay(2);
}
