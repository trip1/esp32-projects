#include "scanner_runtime_config.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>

#include "scanner_config_logic.h"

#ifndef SETUP_BUTTON_PIN
#error "SETUP_BUTTON_PIN must be defined by the board environment"
#endif

namespace {
constexpr uint32_t kMagic = 0x53434e32U;
constexpr uint16_t kVersion = 1U;
constexpr size_t kMaximumRequestBytes = 2048U;
constexpr size_t kMaximumHeaderBytes = 1024U;
constexpr size_t kMaximumBodyBytes = 1024U;
constexpr uint32_t kProvisioningTimeoutMs = 10U * 60U * 1000U;
constexpr uint32_t kRecoveryWindowMs = 5000U;
constexpr uint32_t kRecoveryHoldMs = 2000U;
constexpr uint32_t kRejectedPendingMarker = 0x52504e44U;
constexpr size_t kSetupPasswordCharacters = 8U;
constexpr char kSetupPasswordAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static_assert(sizeof(kSetupPasswordAlphabet) - 1U == 32U, "password alphabet must preserve unbiased five-bit selection");
RTC_DATA_ATTR uint32_t rejected_pending_marker = 0U;

struct FormFields {
    char csrf[17]{};
    char wifi_ssid[33]{};
    char wifi_password[64]{};
    char mqtt_host[129]{};
    char mqtt_port[6]{};
    char mqtt_username[65]{};
    char mqtt_password[129]{};
    char topic_prefix[97]{};
    uint16_t seen = 0U;
};

constexpr uint16_t kCsrf = 1U << 0U;
constexpr uint16_t kSsid = 1U << 1U;
constexpr uint16_t kWifiPassword = 1U << 2U;
constexpr uint16_t kMqttHost = 1U << 3U;
constexpr uint16_t kMqttPort = 1U << 4U;
constexpr uint16_t kMqttUsername = 1U << 5U;
constexpr uint16_t kMqttPassword = 1U << 6U;
constexpr uint16_t kTopicPrefix = 1U << 7U;
constexpr uint16_t kRequired = (1U << 8U) - 1U;

DNSServer dns;
WiFiServer server(80);
bool provisioning = false;
bool restart_requested = false;
uint32_t provisioning_started_ms = 0U;
char csrf_token[17]{};

bool terminated(const char* value, size_t capacity) {
    return std::memchr(value, '\0', capacity) != nullptr;
}

uint32_t checksum(const ScannerRuntimeConfig& value) {
    return scannerConfigCrc32(reinterpret_cast<const uint8_t*>(&value), offsetof(ScannerRuntimeConfig, crc32));
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

bool decodeFormValue(const char* input, size_t length, char* output, size_t capacity) {
    size_t written = 0U;
    for (size_t index = 0U; index < length; ++index) {
        unsigned char value = static_cast<unsigned char>(input[index]);
        if (value == '+') value = ' ';
        else if (value == '%') {
            if (index + 2U >= length) return false;
            const auto hex = [](char character) -> int {
                if (character >= '0' && character <= '9') return character - '0';
                if (character >= 'a' && character <= 'f') return character - 'a' + 10;
                if (character >= 'A' && character <= 'F') return character - 'A' + 10;
                return -1;
            };
            const int high = hex(input[++index]);
            const int low = hex(input[++index]);
            if (high < 0 || low < 0) return false;
            value = static_cast<unsigned char>((high << 4) | low);
        }
        if (value < 0x20U || value > 0x7eU || written + 1U >= capacity) return false;
        output[written++] = static_cast<char>(value);
    }
    output[written] = '\0';
    return true;
}

bool assignField(FormFields& fields, const char* name, size_t name_length,
                 const char* value, size_t value_length) {
    struct Field {
        const char* name;
        uint16_t bit;
        char* output;
        size_t capacity;
    } definitions[] = {
        {"csrf", kCsrf, fields.csrf, sizeof(fields.csrf)},
        {"wifi_ssid", kSsid, fields.wifi_ssid, sizeof(fields.wifi_ssid)},
        {"wifi_password", kWifiPassword, fields.wifi_password, sizeof(fields.wifi_password)},
        {"mqtt_host", kMqttHost, fields.mqtt_host, sizeof(fields.mqtt_host)},
        {"mqtt_port", kMqttPort, fields.mqtt_port, sizeof(fields.mqtt_port)},
        {"mqtt_username", kMqttUsername, fields.mqtt_username, sizeof(fields.mqtt_username)},
        {"mqtt_password", kMqttPassword, fields.mqtt_password, sizeof(fields.mqtt_password)},
        {"topic_prefix", kTopicPrefix, fields.topic_prefix, sizeof(fields.topic_prefix)},
    };
    for (const auto& definition : definitions) {
        if (std::strlen(definition.name) != name_length || std::memcmp(name, definition.name, name_length) != 0) continue;
        if ((fields.seen & definition.bit) != 0U || !decodeFormValue(value, value_length, definition.output, definition.capacity)) return false;
        fields.seen |= definition.bit;
        return true;
    }
    return false;
}

bool parseForm(const char* body, size_t length, FormFields& fields) {
    if (body == nullptr || length == 0U || length > kMaximumBodyBytes) return false;
    size_t start = 0U;
    unsigned count = 0U;
    while (start < length) {
        if (++count > 8U) return false;
        size_t end = start;
        while (end < length && body[end] != '&') ++end;
        size_t equals = start;
        while (equals < end && body[equals] != '=') ++equals;
        if (equals == start || equals == end || !assignField(fields, body + start, equals - start, body + equals + 1U, end - equals - 1U)) return false;
        start = end + 1U;
    }
    return fields.seen == kRequired;
}

bool validCsrfBody(const char* body, size_t length) {
    static constexpr char prefix[] = "csrf=";
    if (length <= sizeof(prefix) - 1U || length > sizeof(prefix) - 1U + 16U ||
        std::memcmp(body, prefix, sizeof(prefix) - 1U) != 0) return false;
    char decoded[17]{};
    return decodeFormValue(body + sizeof(prefix) - 1U, length - (sizeof(prefix) - 1U), decoded, sizeof(decoded)) &&
           std::strcmp(decoded, csrf_token) == 0;
}

String setupPage(const char* message = nullptr) {
    String page;
    page.reserve(2600);
    page += F("<!doctype html><meta name=viewport content='width=device-width'><title>BLE Scanner Setup</title>"
              "<style>body{font:16px system-ui;max-width:36rem;margin:2rem auto;padding:0 1rem;background:#0b1020;color:#e8eefc}"
              "main{background:#151d33;padding:1.25rem;border-radius:16px}label{display:block;margin:.8rem 0}"
              "input{box-sizing:border-box;width:100%;padding:.7rem;border:1px solid #3c4a70;border-radius:8px;background:#0b1020;color:#fff}"
              "button{padding:.75rem 1rem;border:0;border-radius:8px;background:#62d6a7;color:#06130e;font-weight:700}</style>"
              "<main><h1>BLE Proximity Scanner setup</h1><p>Configure Wi-Fi and a trusted-LAN MQTT broker. Settings are tested before replacing active configuration.</p>");
    if (message != nullptr) {
        page += F("<p><strong>");
        page += message;
        page += F("</strong></p>");
    }
    page += F("<form method=post action=/save><input type=hidden name=csrf value='");
    page += csrf_token;
    page += F("'><label>Wi-Fi name<input name=wifi_ssid maxlength=32 required></label>"
              "<label>Wi-Fi password<input type=password name=wifi_password maxlength=63></label>"
              "<label>MQTT host<input name=mqtt_host maxlength=128 required></label>"
              "<label>MQTT port<input name=mqtt_port type=number min=1 max=65535 value=1883 required></label>"
              "<label>MQTT username<input name=mqtt_username maxlength=64></label>"
              "<label>MQTT password<input type=password name=mqtt_password maxlength=128></label>"
              "<label>Topic prefix<input name=topic_prefix maxlength=96 value='esp32/ble-sightings' required></label>"
              "<button>Save and test</button></form>"
              "<form method=post action=/clear><input type=hidden name=csrf value='");
    page += csrf_token;
    page += F("'><button>Erase configuration</button></form></main>");
    return page;
}

void sendHttp(WiFiClient& client, int status, const char* reason, const String& body) {
    client.printf("HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",
                  status, reason, static_cast<unsigned>(body.length()));
    client.print(body);
}

bool createCandidate(const FormFields& fields, ScannerRuntimeConfig& candidate) {
    uint32_t port = 0U;
    if (!parseUnsigned(fields.mqtt_port, 65535U, port) || port == 0U) return false;
    candidate = {};
    candidate.magic = kMagic;
    candidate.version = kVersion;
    candidate.mqtt_port = static_cast<uint16_t>(port);
    if (!copyText(fields.wifi_ssid, std::strlen(fields.wifi_ssid), candidate.wifi_ssid) ||
        !copyText(fields.wifi_password, std::strlen(fields.wifi_password), candidate.wifi_password) ||
        !copyText(fields.mqtt_host, std::strlen(fields.mqtt_host), candidate.mqtt_host) ||
        !copyText(fields.mqtt_username, std::strlen(fields.mqtt_username), candidate.mqtt_username) ||
        !copyText(fields.mqtt_password, std::strlen(fields.mqtt_password), candidate.mqtt_password) ||
        !copyText(fields.topic_prefix, std::strlen(fields.topic_prefix), candidate.topic_prefix)) return false;
    candidate.crc32 = checksum(candidate);
    return scannerConfigIsValid(candidate);
}

void handlePost(WiFiClient& client, const char* path, const char* body, size_t length) {
    if (std::strcmp(path, "/clear") == 0) {
        if (!validCsrfBody(body, length)) sendHttp(client, 400, "Bad Request", setupPage("Request rejected."));
        else if (!scannerClearConfigs()) sendHttp(client, 503, "Unavailable", setupPage("NVS clear failed."));
        else {
            sendHttp(client, 200, "OK", setupPage("Configuration erased. Restarting."));
            restart_requested = true;
        }
        return;
    }
    if (std::strcmp(path, "/save") != 0) {
        sendHttp(client, 404, "Not Found", setupPage());
        return;
    }
    FormFields fields{};
    ScannerRuntimeConfig candidate{};
    if (!parseForm(body, length, fields) || std::strcmp(fields.csrf, csrf_token) != 0 || !createCandidate(fields, candidate)) {
        sendHttp(client, 400, "Bad Request", setupPage("Check all settings and field lengths."));
    } else if (!scannerStoreConfig("pending", candidate)) {
        sendHttp(client, 503, "Unavailable", setupPage("Pending settings could not be stored and verified."));
    } else {
        rejected_pending_marker = 0U;
        sendHttp(client, 200, "OK", setupPage("Settings saved. Restarting to test Wi-Fi and MQTT."));
        restart_requested = true;
    }
}

bool readRequest(WiFiClient& client, char* request, size_t capacity, size_t& used, size_t& header_end,
                 size_t& content_length, char* method, size_t method_capacity, char* path, size_t path_capacity) {
    const uint32_t started = millis();
    header_end = 0U;
    content_length = 0U;
    while (client.connected() && static_cast<uint32_t>(millis() - started) < 2000U) {
        while (client.available()) {
            if (used + 1U >= capacity) return false;
            request[used++] = static_cast<char>(client.read());
            request[used] = '\0';
            if (header_end == 0U && used >= 4U && std::memcmp(request + used - 4U, "\r\n\r\n", 4U) == 0) {
                header_end = used;
                if (header_end > kMaximumHeaderBytes) return false;
                bool has_content_length = false;
                if (!scannerParseHttpRequest(request, header_end, kMaximumBodyBytes, method, method_capacity,
                                             path, path_capacity, content_length, has_content_length)) return false;

            }
            if (header_end != 0U && used >= header_end + content_length) return true;
        }
        delay(1);
    }
    return header_end != 0U && used >= header_end + content_length;
}

void makeRandomHex(char* output, size_t bytes) {
    static constexpr char hex[] = "0123456789abcdef";
    for (size_t index = 0U; index < bytes; ++index) {
        const uint8_t value = static_cast<uint8_t>(esp_random());
        output[index * 2U] = hex[value >> 4U];
        output[index * 2U + 1U] = hex[value & 0x0fU];
    }
    output[bytes * 2U] = '\0';
}

template <size_t N>
void makeReadableSetupPassword(char (&output)[N]) {
    static_assert(N >= kSetupPasswordCharacters + 1U, "setup password buffer is too small");
    for (size_t index = 0U; index < kSetupPasswordCharacters; ++index) {
        output[index] = kSetupPasswordAlphabet[esp_random() & 31U];
    }
    output[kSetupPasswordCharacters] = '\0';
}
}  // namespace

bool scannerConfigIsValid(const ScannerRuntimeConfig& value) {
    if (value.magic != kMagic || value.version != kVersion || value.crc32 != checksum(value) ||
        !terminated(value.wifi_ssid, sizeof(value.wifi_ssid)) || !terminated(value.wifi_password, sizeof(value.wifi_password)) ||
        !terminated(value.mqtt_host, sizeof(value.mqtt_host)) || !terminated(value.mqtt_username, sizeof(value.mqtt_username)) ||
        !terminated(value.mqtt_password, sizeof(value.mqtt_password)) || !terminated(value.topic_prefix, sizeof(value.topic_prefix))) return false;
    return scannerValidWifiSsid(value.wifi_ssid) && scannerValidWifiPassword(value.wifi_password) &&
           scannerValidMqttHost(value.mqtt_host) && value.mqtt_port > 0U &&
           scannerValidCredential(value.mqtt_username, sizeof(value.mqtt_username) - 1U) &&
           scannerValidCredential(value.mqtt_password, sizeof(value.mqtt_password) - 1U) &&
           !(value.mqtt_username[0] == '\0' && value.mqtt_password[0] != '\0') && scannerValidTopicPrefix(value.topic_prefix);
}

bool scannerLoadConfig(const char* key, ScannerRuntimeConfig& value) {
    Preferences preferences;
    if (!preferences.begin("blescanner", true)) return false;
    const size_t length = preferences.getBytesLength(key);
    const size_t read = length == sizeof(value) ? preferences.getBytes(key, &value, sizeof(value)) : 0U;
    preferences.end();
    return read == sizeof(value) && scannerConfigIsValid(value);
}

bool scannerStoreConfig(const char* key, const ScannerRuntimeConfig& value) {
    Preferences preferences;
    if (!scannerConfigIsValid(value) || !preferences.begin("blescanner", false)) return false;
    const bool stored = preferences.putBytes(key, &value, sizeof(value)) == sizeof(value);
    preferences.end();
    if (!stored) return false;
    ScannerRuntimeConfig verified{};
    return scannerLoadConfig(key, verified) && std::memcmp(&verified, &value, sizeof(value)) == 0;
}

bool scannerRemoveConfig(const char* key) {
    Preferences preferences;
    if (!preferences.begin("blescanner", false)) return false;
    const bool removed = !preferences.isKey(key) || preferences.remove(key);
    preferences.end();
    return removed;
}

bool scannerClearConfigs() {
    Preferences preferences;
    if (!preferences.begin("blescanner", false)) return false;
    const bool cleared = preferences.clear();
    preferences.end();
    if (cleared) rejected_pending_marker = 0U;
    return cleared;
}

bool scannerPromotePending(const ScannerRuntimeConfig& pending) {
    if (!scannerStoreConfig("active", pending)) return false;
    rejected_pending_marker = scannerRemoveConfig("pending") ? 0U : kRejectedPendingMarker;
    return true;
}

bool scannerPendingSuppressed() {
    return rejected_pending_marker == kRejectedPendingMarker;
}

bool scannerRejectPending() {
    const bool removed = scannerRemoveConfig("pending");
    rejected_pending_marker = removed ? 0U : kRejectedPendingMarker;
    return removed;
}

bool scannerRecoveryRequested() {
    Serial.println("Hold BOOT now for two seconds to open setup");
    const uint32_t started = millis();
    uint32_t held_since = 0U;
    while (static_cast<uint32_t>(millis() - started) < kRecoveryWindowMs) {
        if (digitalRead(SETUP_BUTTON_PIN) == LOW) {
            if (held_since == 0U) held_since = millis();
            if (static_cast<uint32_t>(millis() - held_since) >= kRecoveryHoldMs) return true;
        } else held_since = 0U;
        delay(10);
    }
    return false;
}

bool scannerStartProvisioning() {
    uint8_t mac[6]{};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char ssid[32];
    char password[kSetupPasswordCharacters + 1U];
    std::snprintf(ssid, sizeof(ssid), "BLE-Scanner-Setup-%02X%02X%02X", mac[3], mac[4], mac[5]);
    WiFi.mode(WIFI_AP);
    makeReadableSetupPassword(password);
    makeRandomHex(csrf_token, 8U);
    if (!WiFi.softAP(ssid, password, 1, false, 1)) return false;
    if (!dns.start(53, "*", WiFi.softAPIP())) {
        WiFi.softAPdisconnect(true);
        return false;
    }
    server.begin();
    provisioning = true;
    provisioning_started_ms = millis();
    Serial.printf("Provisioning: join %s with password %s, then open http://%s\n",
                  ssid, password, WiFi.softAPIP().toString().c_str());
    return true;
}

bool scannerProvisioningActive() {
    return provisioning;
}

void scannerHandleProvisioning() {
    if (!provisioning) return;
    dns.processNextRequest();
    WiFiClient client = server.accept();
    if (client) {
        char request[kMaximumRequestBytes + 1U]{};
        char method[8]{};
        char path[64]{};
        size_t used = 0U;
        size_t header_end = 0U;
        size_t content_length = 0U;
        if (!readRequest(client, request, sizeof(request), used, header_end, content_length,
                         method, sizeof(method), path, sizeof(path))) {
            sendHttp(client, 413, "Payload Too Large", setupPage("Request too large or incomplete."));
        } else if (std::strcmp(method, "GET") == 0) {
            sendHttp(client, 200, "OK", setupPage());
        } else if (std::strcmp(method, "POST") == 0 && content_length > 0U) {
            handlePost(client, path, request + header_end, content_length);
        } else sendHttp(client, 405, "Method Not Allowed", setupPage());
        delay(1);
        client.stop();
    }
    if (restart_requested || static_cast<uint32_t>(millis() - provisioning_started_ms) >= kProvisioningTimeoutMs) {
        delay(250);
        ESP.restart();
    }
}
