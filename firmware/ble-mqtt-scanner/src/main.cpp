#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <string>

#include "config.h"
#include "device_registry.h"
#include "presence_tracker.h"
#include "scanner_runtime_config.h"

namespace {
constexpr char kLogPath[] = "/sightings.jsonl";
constexpr char kOldLogPath[] = "/sightings.1.jsonl";
constexpr std::size_t kPayloadSize = 1536;

struct RawObservation {
    char address[18];
    char name[64];
    char manufacturer_data[129];
    char service_uuids[193];
    int8_t rssi;
    uint8_t address_type;
};

QueueHandle_t observation_queue = nullptr;
DeviceRegistry registry(DEVICE_REGISTRY_CAPACITY);
PresenceTracker presence_tracker(PRESENCE_TRACKER_CAPACITY, PRESENCE_ENTER_RSSI);
WiFiClient network_client;
PubSubClient mqtt(network_client);
NimBLEScan* scanner = nullptr;
char device_id[24] = {};
char event_topic[160] = {};
char status_topic[160] = {};
uint32_t last_wifi_attempt_ms = 0;
uint32_t last_mqtt_attempt_ms = 0;
uint32_t last_scan_attempt_ms = 0;
uint32_t last_overflow_report_ms = 0;
std::atomic<uint32_t> dropped_observations{0};
std::atomic<bool> scan_restart_requested{false};
bool time_configured = false;
ScannerRuntimeConfig runtime_config{};
bool runtime_config_ready = false;

std::string bytesToHex(const std::string& bytes) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const unsigned char value : bytes) {
        result.push_back(hex[value >> 4]);
        result.push_back(hex[value & 0x0f]);
    }
    return result;
}

void copyTruncated(char* destination, std::size_t size, const std::string& source) {
    if (size == 0) return;
    const std::size_t count = std::min(size - 1, source.size());
    std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
}

class ScanCallbacks final : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        RawObservation observation{};
        copyTruncated(observation.address, sizeof(observation.address), device->getAddress().toString());
        copyTruncated(observation.name, sizeof(observation.name), device->getName());
        observation.rssi = device->getRSSI();
        observation.address_type = device->getAddressType();

        if (device->getManufacturerDataCount() > 0) {
            copyTruncated(
                observation.manufacturer_data,
                sizeof(observation.manufacturer_data),
                bytesToHex(device->getManufacturerData()));
        }

        std::string services;
        for (uint8_t index = 0; index < device->getServiceUUIDCount(); ++index) {
            if (!services.empty()) services.push_back(',');
            services += device->getServiceUUID(index).toString();
        }
        copyTruncated(observation.service_uuids, sizeof(observation.service_uuids), services);
        if (xQueueSend(observation_queue, &observation, 0) != pdTRUE) {
            dropped_observations.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void onScanEnd(const NimBLEScanResults&, int reason) override {
        Serial.printf("BLE scan ended (%d); restarting\n", reason);
        scan_restart_requested.store(true, std::memory_order_release);
    }
} scan_callbacks;

bool ensureLogCapacity(std::size_t upcoming_bytes) {
    File log = LittleFS.open(kLogPath, FILE_READ);
    const std::size_t size = log ? log.size() : 0;
    if (log) log.close();
    if (size + upcoming_bytes <= LOCAL_LOG_MAX_BYTES) return true;
    if (LittleFS.exists(kOldLogPath) && !LittleFS.remove(kOldLogPath)) return false;
    if (LittleFS.exists(kLogPath) && !LittleFS.rename(kLogPath, kOldLogPath)) return false;
    return true;
}

bool publishAndLog(const RawObservation& observation, DeviceState& state) {
    JsonDocument document;
    document["scanner_id"] = device_id;
    document["address"] = observation.address;
    document["address_type"] = observation.address_type;
    document["name"] = state.name;
    document["rssi"] = state.rssi;
    document["manufacturer_data"] = observation.manufacturer_data;
    document["service_uuids"] = observation.service_uuids;
    document["first_seen_ms"] = state.first_seen_ms;
    document["last_seen_ms"] = state.last_seen_ms;
    document["seen_count"] = state.seen_count;
    const time_t now = time(nullptr);
    if (now > 1700000000) document["seen_at_unix"] = static_cast<int64_t>(now);

    char payload[kPayloadSize];
    const std::size_t length = serializeJson(document, payload, sizeof(payload));
    if (length == 0 || length >= sizeof(payload)) {
        Serial.println("Sighting payload exceeded buffer; dropped");
        return false;
    }

    const bool published = mqtt.connected() && mqtt.publish(event_topic, payload, false);
    document["mqtt_published"] = published;
    const std::size_t log_length = serializeJson(document, payload, sizeof(payload));
    if (log_length > 0 && log_length < sizeof(payload)) {
        File log;
        if (ensureLogCapacity(log_length + 1)) log = LittleFS.open(kLogPath, FILE_APPEND);
        if (log) {
            const bool wrote_payload = log.write(reinterpret_cast<const uint8_t*>(payload), log_length) == log_length;
            const bool wrote_newline = log.write('\n') == 1;
            if (!wrote_payload || !wrote_newline) Serial.println("Local sighting log write failed");
            log.close();
        } else {
            Serial.println("Local sighting log open or rotation failed");
        }
    }

    Serial.printf("BLE %s name=\"%s\" rssi=%d count=%lu mqtt=%s\n",
                  observation.address,
                  state.name.c_str(),
                  state.rssi,
                  static_cast<unsigned long>(state.seen_count),
                  published ? "sent" : "offline");
    return published;
}

void publishPendingPresence() {
    if (!mqtt.connected()) return;

    for (const auto& state : presence_tracker.states()) {
        if (!state.dirty) continue;

        char address_token[13] = {};
        std::size_t token_index = 0;
        for (const char character : state.address) {
            if (character != ':' && token_index < sizeof(address_token) - 1) {
                address_token[token_index++] = character;
            }
        }

        char topic[192];
        const int topic_length = std::snprintf(
            topic, sizeof(topic), "%s/%s/presence/%s", runtime_config.topic_prefix, device_id, address_token);
        if (topic_length <= 0 || static_cast<std::size_t>(topic_length) >= sizeof(topic)) {
            Serial.println("Presence topic exceeded buffer; dropped");
            continue;
        }

        JsonDocument document;
        document["scanner_id"] = device_id;
        document["address"] = state.address;
        document["name"] = state.name;
        document["rssi"] = state.rssi;
        document["present"] = state.present;
        document["state"] = state.present ? "present" : "away";
        document["last_seen_ms"] = state.last_seen_ms;
        const time_t now = time(nullptr);
        if (now > 1700000000) document["updated_at_unix"] = static_cast<int64_t>(now);

        char payload[512];
        const std::size_t length = serializeJson(document, payload, sizeof(payload));
        if (length == 0 || length >= sizeof(payload)) {
            Serial.println("Presence payload exceeded buffer; dropped");
            continue;
        }

        if (mqtt.publish(topic, payload, true)) {
            presence_tracker.markPublished(state.address);
            Serial.printf("Presence %s %s rssi=%d\n",
                          state.address.c_str(), state.present ? "entered" : "exited", state.rssi);
        }
    }
}

void connectWiFi(uint32_t now_ms) {
    if (WiFi.status() == WL_CONNECTED || !runtime_config_ready) return;
    if (now_ms - last_wifi_attempt_ms < 10000) return;
    last_wifi_attempt_ms = now_ms;
    Serial.printf("Connecting Wi-Fi to %s\n", runtime_config.wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(runtime_config.wifi_ssid, runtime_config.wifi_password);
}

void connectMqtt(uint32_t now_ms) {
    if (WiFi.status() != WL_CONNECTED || mqtt.connected() || !runtime_config_ready) return;
    if (now_ms - last_mqtt_attempt_ms < 5000) return;
    last_mqtt_attempt_ms = now_ms;

    bool connected;
    if (runtime_config.mqtt_username[0] == '\0') {
        connected = mqtt.connect(device_id, status_topic, 0, true, "offline");
    } else {
        connected = mqtt.connect(
            device_id, runtime_config.mqtt_username, runtime_config.mqtt_password,
            status_topic, 0, true, "offline");
    }
    if (connected) {
        mqtt.publish(status_topic, "online", true);
        Serial.printf("MQTT connected; publishing to %s\n", event_topic);
    } else {
        Serial.printf("MQTT connection failed: %d\n", mqtt.state());
    }
}

void ensureScanning(uint32_t now_ms) {
    if (scanner == nullptr || (scanner->isScanning() && !scan_restart_requested.load(std::memory_order_acquire))) return;
    if (now_ms - last_scan_attempt_ms < 1000) return;
    last_scan_attempt_ms = now_ms;
    scan_restart_requested.store(false, std::memory_order_release);
    if (!scanner->start(SCAN_DURATION_MS, false, true)) {
        scan_restart_requested.store(true, std::memory_order_release);
        Serial.println("BLE scan start failed; retrying");
    }
}

void reportQueueOverflow(uint32_t now_ms) {
    const uint32_t overflow_count = dropped_observations.load(std::memory_order_relaxed);
    if (overflow_count == 0 || now_ms - last_overflow_report_ms < 60000) return;
    last_overflow_report_ms = now_ms;
    Serial.printf("BLE observation queue overflows: %lu\n", static_cast<unsigned long>(overflow_count));
    if (mqtt.connected()) {
        char payload[96];
        std::snprintf(payload, sizeof(payload), "{\"status\":\"online\",\"queue_overflow_count\":%lu}",
                      static_cast<unsigned long>(overflow_count));
        mqtt.publish(status_topic, payload, true);
    }
}

const char* identityPrefix() {
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

void configureIdentity(const ScannerRuntimeConfig& value) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    std::snprintf(device_id, sizeof(device_id), "%s-%02x%02x%02x%02x%02x%02x",
                  identityPrefix(), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    std::snprintf(event_topic, sizeof(event_topic), "%s/%s/events", value.topic_prefix, device_id);
    std::snprintf(status_topic, sizeof(status_topic), "%s/%s/status", value.topic_prefix, device_id);
}

void configureMqtt(const ScannerRuntimeConfig& value) {
    mqtt.setServer(value.mqtt_host, value.mqtt_port);
    mqtt.setBufferSize(2048);
    mqtt.setKeepAlive(30);
    mqtt.setSocketTimeout(1);
    network_client.setTimeout(1000);
    network_client.setConnectionTimeout(1000);
}

bool validatePendingConfiguration(const ScannerRuntimeConfig& value) {
    configureIdentity(value);
    configureMqtt(value);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(value.wifi_ssid, value.wifi_password);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && static_cast<uint32_t>(millis() - started) < 15000U) delay(100);
    if (WiFi.status() != WL_CONNECTED) return false;
    const bool connected = value.mqtt_username[0] == '\0'
        ? mqtt.connect(device_id)
        : mqtt.connect(device_id, value.mqtt_username, value.mqtt_password);
    const bool published = connected && mqtt.publish(status_topic, "validating", false);
    if (connected) mqtt.disconnect();
    WiFi.disconnect(true, false);
    return published;
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(250);
    pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
    runtime_config_ready = scannerLoadConfig("active", runtime_config);
    ScannerRuntimeConfig pending{};
    const bool pending_valid = scannerLoadConfig("pending", pending);
    const bool pending_matches_active = pending_valid && runtime_config_ready &&
        std::memcmp(&pending, &runtime_config, sizeof(pending)) == 0;
    const bool pending_ready = pending_valid && !pending_matches_active && !scannerPendingSuppressed();
    if (!pending_valid) scannerRemoveConfig("pending");
    else if (pending_matches_active && !scannerRemoveConfig("pending")) {
        Serial.println("Already-promoted pending record could not be removed; active configuration remains authoritative");
    }
    if ((runtime_config_ready || pending_ready) && scannerRecoveryRequested()) {
        if (!scannerStartProvisioning()) ESP.restart();
        return;
    }
    if (pending_ready) {
        if (validatePendingConfiguration(pending) && scannerPromotePending(pending)) {
            runtime_config = pending;
            runtime_config_ready = true;
            Serial.println("Pending Wi-Fi and MQTT settings verified and promoted");
        } else {
            scannerRejectPending();
            Serial.println("Pending settings rejected; active settings preserved");
        }
    }
    if (!runtime_config_ready) {
        if (!scannerStartProvisioning()) ESP.restart();
        return;
    }
    configureIdentity(runtime_config);
    Serial.printf("Starting %s\n", device_id);

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed; local logging disabled");
    }

    observation_queue = xQueueCreate(64, sizeof(RawObservation));
    if (observation_queue == nullptr) {
        Serial.println("Failed to allocate observation queue");
        abort();
    }

    configureMqtt(runtime_config);

    NimBLEDevice::init("");
    scanner = NimBLEDevice::getScan();
    scanner->setScanCallbacks(&scan_callbacks, false);
    scanner->setActiveScan(true);
    scanner->setInterval(100);
    scanner->setWindow(80);
    scanner->setMaxResults(0);
    scan_restart_requested.store(true, std::memory_order_release);
}

void loop() {
    if (scannerProvisioningActive()) {
        scannerHandleProvisioning();
        delay(2);
        return;
    }
    const uint32_t now_ms = millis();
    connectWiFi(now_ms);
    if (WiFi.status() == WL_CONNECTED && !time_configured) {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        time_configured = true;
    }
    mqtt.loop();

    RawObservation observation{};
    while (xQueueReceive(observation_queue, &observation, 0) == pdTRUE) {
        const auto presence = presence_tracker.observe(
            observation.address,
            observation.name,
            observation.rssi,
            now_ms);
        if (presence.transition == PresenceTransition::Entered) {
            Serial.printf("BLE proximity enter: %s name=\"%s\" rssi=%d\n",
                          observation.address, observation.name, observation.rssi);
        }

        auto result = registry.observe(
            observation.address,
            observation.name,
            observation.rssi,
            now_ms,
            SIGHTING_INTERVAL_MS);
        if (result.state != nullptr && result.should_publish) {
            publishAndLog(observation, *result.state);
        }
    }
    const auto exits = presence_tracker.expire(now_ms, PRESENCE_EXIT_TIMEOUT_MS);
    for (const auto& exit : exits) {
        Serial.printf("BLE proximity exit: %s name=\"%s\"\n",
                      exit.state->address.c_str(), exit.state->name.c_str());
    }
    ensureScanning(now_ms);
    reportQueueOverflow(now_ms);
    publishPendingPresence();
    if (uxQueueMessagesWaiting(observation_queue) == 0) connectMqtt(now_ms);
    delay(10);
}
