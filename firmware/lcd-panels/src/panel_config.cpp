#include "panel_config.h"
#include "setup_http.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <nvs.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

#ifndef PANEL_KIND
#error "PANEL_KIND is required"
#endif
#ifndef SETUP_BUTTON_PIN
#error "SETUP_BUTTON_PIN is required"
#endif

namespace {
constexpr uint32_t kMagic = 0x4c434450U;
constexpr uint16_t kVersion = 1U;
constexpr size_t kMaxHeader = 2048U;
constexpr size_t kMaxBody = 1024U;
constexpr size_t kMaxRequest = kMaxHeader + kMaxBody;
constexpr uint32_t kPortalTimeoutMs = 10U * 60U * 1000U;
constexpr uint32_t kRejectedPendingMarker = 0x504e4458U;
constexpr size_t kSetupPasswordCharacters = 8U;
constexpr char kSetupPasswordAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static_assert(sizeof(kSetupPasswordAlphabet) - 1U == 32U, "setup password alphabet must have 32 symbols");

struct StoredConfig {
    uint32_t magic;
    uint16_t version;
    uint8_t kind;
    uint8_t reserved;
    PanelConfig value;
    uint32_t crc32;
};

RTC_DATA_ATTR uint32_t rejected_pending_marker = 0U;
RTC_DATA_ATTR uint32_t force_backup_marker = 0U;
DNSServer dns;
WiFiServer server(80);
bool provisioning = false;
bool restart_requested = false;
uint32_t portal_started_ms = 0U;
char csrf_token[17]{};
char request_buffer[kMaxRequest + 1U]{};

void clearRequestBuffer() {
    volatile char* cursor = request_buffer;
    for (size_t index = 0U; index < sizeof(request_buffer); ++index) cursor[index] = '\0';
}
struct RequestBufferGuard { ~RequestBufferGuard() { clearRequestBuffer(); } };

PanelKind kind() { return static_cast<PanelKind>(PANEL_KIND); }
uint32_t checksum(const StoredConfig& value) {
    return panelCrc32(reinterpret_cast<const unsigned char*>(&value), offsetof(StoredConfig, crc32));
}

struct Fields {
    char csrf[17]{};
    PanelConfig value{};
    char mqtt_port[6]{};
    uint16_t seen = 0U;
};

uint16_t requiredMask() {
    if (kind() == PanelKind::Mqtt) return 0x01ffU;
    if (kind() == PanelKind::Unifi) return 0x0207U;
    if (kind() == PanelKind::Weather) return 0x0c07U;
    return 0x0007U;
}

bool assignField(Fields& fields, const char* name, size_t name_length, const char* value, size_t value_length) {
    struct Definition { const char* name; uint16_t bit; char* output; size_t capacity; } definitions[] = {
        {"csrf", 0x0001U, fields.csrf, sizeof(fields.csrf)},
        {"wifi_ssid", 0x0002U, fields.value.wifi_ssid, sizeof(fields.value.wifi_ssid)},
        {"wifi_password", 0x0004U, fields.value.wifi_password, sizeof(fields.value.wifi_password)},
        {"mqtt_host", 0x0008U, fields.value.mqtt_host, sizeof(fields.value.mqtt_host)},
        {"mqtt_port", 0x0010U, fields.mqtt_port, sizeof(fields.mqtt_port)},
        {"mqtt_username", 0x0020U, fields.value.mqtt_username, sizeof(fields.value.mqtt_username)},
        {"mqtt_password", 0x0040U, fields.value.mqtt_password, sizeof(fields.value.mqtt_password)},
        {"mqtt_topic", 0x0080U, fields.value.mqtt_topic, sizeof(fields.value.mqtt_topic)},
        {"label", 0x0100U, fields.value.label, sizeof(fields.value.label)},
        {"unifi_url", 0x0200U, fields.value.unifi_url, sizeof(fields.value.unifi_url)},
        {"latitude", 0x0400U, fields.value.latitude, sizeof(fields.value.latitude)},
        {"longitude", 0x0800U, fields.value.longitude, sizeof(fields.value.longitude)},
    };
    const uint16_t allowed = requiredMask();
    for (const auto& definition : definitions) {
        if (std::strlen(definition.name) != name_length || std::memcmp(name, definition.name, name_length) != 0) continue;
        if ((allowed & definition.bit) == 0U || (fields.seen & definition.bit) != 0U) return false;
        if (value_length >= definition.capacity) return false;
        char temporary[161]{};
        if (definition.capacity > sizeof(temporary)) return false;
        size_t written = 0U;
        for (size_t index = 0U; index < value_length; ++index) {
            unsigned char decoded = static_cast<unsigned char>(value[index]);
            if (decoded == '+') decoded = ' ';
            else if (decoded == '%') {
                if (index + 2U >= value_length) return false;
                auto hex = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
                const int high = hex(value[++index]); const int low = hex(value[++index]);
                if (high < 0 || low < 0) return false;
                decoded = static_cast<unsigned char>((high << 4) | low);
            }
            if (decoded < 0x20U || decoded > 0x7eU || written + 1U >= definition.capacity) return false;
            temporary[written++] = static_cast<char>(decoded);
        }
        temporary[written] = '\0';
        std::memcpy(definition.output, temporary, written + 1U);
        fields.seen |= definition.bit;
        return true;
    }
    return false;
}

bool parseForm(const char* body, size_t length, Fields& fields) {
    if (body == nullptr || length == 0U || length > kMaxBody) return false;
    size_t start = 0U;
    unsigned count = 0U;
    while (start < length) {
        if (++count > 9U) return false;
        size_t end = start;
        while (end < length && body[end] != '&') ++end;
        size_t equals = start;
        while (equals < end && body[equals] != '=') ++equals;
        if (equals == start || equals == end || !assignField(fields, body + start, equals - start, body + equals + 1U, end - equals - 1U)) return false;
        start = end + 1U;
    }
    if (fields.seen != requiredMask()) return false;
    if (kind() == PanelKind::Mqtt) {
        unsigned port = 0U;
        if (fields.mqtt_port[0] == '\0') return false;
        for (const char* digit = fields.mqtt_port; *digit != '\0'; ++digit) {
            if (*digit < '0' || *digit > '9') return false;
            port = port * 10U + static_cast<unsigned>(*digit - '0');
            if (port > 65535U) return false;
        }
        fields.value.mqtt_port = static_cast<uint16_t>(port);
    }
    return panelConfigValid(kind(), fields.value);
}

String page(const char* message = nullptr) {
    String output;
    output.reserve(4600U);
    output += F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><style>body{font:16px system-ui;max-width:36rem;margin:2rem auto;padding:0 1rem;background:#0b1020;color:#e8eefc}main{background:#151d33;padding:1.25rem;border-radius:16px}label{display:block;margin:.8rem 0}input{box-sizing:border-box;width:100%;padding:.7rem;background:#0b1020;color:#fff;border:1px solid #3c4a70;border-radius:8px}button{padding:.75rem 1rem;background:#62d6a7;border:0;border-radius:8px;font-weight:700}</style><main><h1>LCD1602 panel setup</h1><p>Settings are validated before replacing the active configuration.</p>");
    if (message != nullptr) { output += F("<p><strong>"); output += message; output += F("</strong></p>"); }
    output += F("<form method=post action=/save enctype=application/x-www-form-urlencoded accept-charset=UTF-8><input type=hidden name=csrf value='"); output += csrf_token;
    output += F("'><label>Wi-Fi name<input name=wifi_ssid maxlength=32 required></label><label>Wi-Fi password<input type=password name=wifi_password maxlength=63></label>");
    if (kind() == PanelKind::Mqtt) {
        output += F("<label>MQTT broker private IPv4<input name=mqtt_host maxlength=64 inputmode=decimal placeholder='10.0.0.2' required></label><label>MQTT port<input name=mqtt_port inputmode=numeric maxlength=5 value=1883 required></label><label>MQTT username<input name=mqtt_username maxlength=64></label><label>MQTT password<input type=password name=mqtt_password maxlength=64></label><label>Exact topic<input name=mqtt_topic maxlength=128 required></label><label>Display label<input name=label maxlength=16 required></label>");
    } else if (kind() == PanelKind::Unifi) {
        output += F("<label>Metrics bridge URL<input name=unifi_url maxlength=160 placeholder='http://10.0.0.2:8090/api/unifi/summary' required></label><p>Plain HTTP is accepted only for a private IPv4 LAN address. The bridge, not this display, holds the UniFi API key.</p>");
    } else if (kind() == PanelKind::Weather) {
        output += F("<label>Latitude<input name=latitude maxlength=16 inputmode=decimal required></label><label>Longitude<input name=longitude maxlength=16 inputmode=decimal required></label>");
    }
    output += F("<button>Save and test</button></form></main>");
    return output;
}

void send(WiFiClient& client, int status, const char* reason, const String& body) {
    client.printf("HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; form-action 'self'\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: same-origin\r\n\r\n", status, reason, static_cast<unsigned>(body.length()));
    client.print(body);
}

void redirectToSetup(WiFiClient& client) {
    client.print(F("HTTP/1.1 302 Found\r\nLocation: http://192.168.4.1/\r\nContent-Length: 0\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n"));
}

SetupHttpResult readRequest(WiFiClient& client, char* request, size_t capacity, size_t& bytes_read, SetupHttpRequest& parsed) {
    size_t used = 0U; bytes_read = 0U;
    const uint32_t started = millis(); SetupHttpResult result = SetupHttpResult::NeedMore;
    while (static_cast<uint32_t>(millis() - started) < 5000U) {
        while (client.available()) {
            if (used + 1U >= capacity) return SetupHttpResult::BodyTooLarge;
            request[used++] = static_cast<char>(client.read()); bytes_read = used; request[used] = '\0';
        }
        result = setupHttpParse(request, used, kMaxHeader, kMaxBody, parsed);
        if (result != SetupHttpResult::NeedMore) return result;
        if (!setupHttpCanRead(client.connected(), static_cast<size_t>(client.available()))) return result;
        delay(1);
    }
    return result;
}

void randomHex(char* output, size_t bytes) {
    static constexpr char hex[] = "0123456789abcdef";
    for (size_t index = 0U; index < bytes; ++index) {
        const uint8_t value = static_cast<uint8_t>(esp_random());
        output[index * 2U] = hex[value >> 4U]; output[index * 2U + 1U] = hex[value & 15U];
    }
    output[bytes * 2U] = '\0';
}

void makeReadableSetupPassword(char (&output)[kSetupPasswordCharacters + 1U]) {
    for (size_t index = 0U; index < kSetupPasswordCharacters; ++index) output[index] = kSetupPasswordAlphabet[esp_random() & 31U];
    output[kSetupPasswordCharacters] = '\0';
}
}  // namespace

static bool loadExactConfig(const char* key, PanelConfig& value) {
    Preferences preferences;
    if (!preferences.begin("lcdpanel", true)) return false;
    StoredConfig stored{};
    const size_t length = preferences.getBytesLength(key);
    const size_t read = length == sizeof(stored) ? preferences.getBytes(key, &stored, sizeof(stored)) : 0U;
    preferences.end();
    if (read != sizeof(stored) || stored.magic != kMagic || stored.version != kVersion || stored.kind != PANEL_KIND
        || stored.reserved != 0U || stored.crc32 != checksum(stored) || !panelConfigValid(kind(), stored.value)) return false;
    value = stored.value;
    return true;
}

bool panelLoadConfig(const char* key, PanelConfig& value) {
    if (std::strcmp(key, "active") == 0 && force_backup_marker == kRejectedPendingMarker
        && loadExactConfig("backup", value)) return true;
    if (loadExactConfig(key, value)) return true;
    return std::strcmp(key, "active") == 0 && loadExactConfig("backup", value);
}

bool panelStoreConfig(const char* key, const PanelConfig& value) {
    if (!panelConfigValid(kind(), value)) return false;
    StoredConfig stored{};
    stored.magic = kMagic; stored.version = kVersion; stored.kind = PANEL_KIND; stored.value = value; stored.crc32 = checksum(stored);
    Preferences preferences;
    if (!preferences.begin("lcdpanel", false)) return false;
    const bool written = preferences.putBytes(key, &stored, sizeof(stored)) == sizeof(stored);
    preferences.end();
    if (!written) return false;
    PanelConfig verified{};
    if (!loadExactConfig(key, verified) || std::memcmp(&verified, &value, sizeof(value)) != 0) {
        if (std::strcmp(key, "active") == 0) {
            PanelConfig backup{};
            force_backup_marker = kRejectedPendingMarker;
            if (loadExactConfig("backup", backup)) {
                StoredConfig restored{};
                restored.magic = kMagic; restored.version = kVersion; restored.kind = PANEL_KIND;
                restored.value = backup; restored.crc32 = checksum(restored);
                bool restored_written = false;
                Preferences restore;
                if (restore.begin("lcdpanel", false)) {
                    restored_written = restore.putBytes("active", &restored, sizeof(restored)) == sizeof(restored);
                    restore.end();
                }
                PanelConfig restored_check{};
                if (!restored_written || !loadExactConfig("active", restored_check)
                    || std::memcmp(&restored_check, &backup, sizeof(backup)) != 0) panelRemoveConfig("active");
            } else panelRemoveConfig("active");
        } else panelRemoveConfig(key);
        Serial.println("Panel configuration read-back verification failed; previous active configuration retained");
        return false;
    }
    return true;
}

bool panelRemoveConfig(const char* key) {
    nvs_handle_t handle = 0;
    if (nvs_open("lcdpanel", NVS_READWRITE, &handle) != ESP_OK) return false;
    size_t length = 0U;
    const esp_err_t status = nvs_get_blob(handle, key, nullptr, &length);
    if (status == ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return true; }
    if (status != ESP_OK) { nvs_close(handle); return false; }
    if (nvs_erase_key(handle, key) != ESP_OK || nvs_commit(handle) != ESP_OK) { nvs_close(handle); return false; }
    length = 0U;
    const bool absent = nvs_get_blob(handle, key, nullptr, &length) == ESP_ERR_NVS_NOT_FOUND;
    nvs_close(handle);
    return absent;
}

bool panelPromotePending(const PanelConfig& value) {
    PanelConfig previous{};
    if (panelLoadConfig("active", previous) && !panelStoreConfig("backup", previous)) return false;
    if (!panelStoreConfig("active", value)) return false;
    force_backup_marker = 0U;
    rejected_pending_marker = panelRemoveConfig("pending") ? 0U : kRejectedPendingMarker;
    return true;
}

bool panelPendingSuppressed() { return rejected_pending_marker == kRejectedPendingMarker; }
void panelBeginPendingValidation() { rejected_pending_marker = kRejectedPendingMarker; }
void panelRejectPending() { rejected_pending_marker = panelRemoveConfig("pending") ? 0U : kRejectedPendingMarker; }

bool panelRecoveryRequested() {
    Serial.println("Hold BOOT now for two seconds to open panel setup");
    const uint32_t started = millis(); uint32_t held_since = 0U;
    while (static_cast<uint32_t>(millis() - started) < 5000U) {
        if (digitalRead(SETUP_BUTTON_PIN) == LOW) {
            if (held_since == 0U) held_since = millis();
            if (static_cast<uint32_t>(millis() - held_since) >= 2000U) return true;
        } else held_since = 0U;
        delay(10);
    }
    return false;
}

bool panelStartProvisioning() {
    uint8_t mac[6]{}; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char ssid[32]{}; char password[kSetupPasswordCharacters + 1U]{};
    std::snprintf(ssid, sizeof(ssid), "LCD-Panel-Setup-%02X%02X%02X", mac[3], mac[4], mac[5]);
    WiFi.mode(WIFI_AP); makeReadableSetupPassword(password); randomHex(csrf_token, 8U);
    if (!WiFi.softAP(ssid, password, 1, false, 1)) return false;
    if (!dns.start(53, "*", WiFi.softAPIP())) { WiFi.softAPdisconnect(true); return false; }
    server.begin();
    if (!server) { dns.stop(); WiFi.softAPdisconnect(true); return false; }
    portal_started_ms = millis(); provisioning = true;
    Serial.printf("Setup network: %s\nSetup password: %s\nOpen http://192.168.4.1/\n", ssid, password);
    return true;
}

bool panelProvisioningActive() { return provisioning; }

void panelHandleProvisioning() {
    if (!provisioning) return;
    dns.processNextRequest();
    if (static_cast<uint32_t>(millis() - portal_started_ms) >= kPortalTimeoutMs) { ESP.restart(); return; }
    if (restart_requested) { delay(200); ESP.restart(); }
    WiFiClient client = server.accept();
    if (!client) return;
    client.setTimeout(2U);
    clearRequestBuffer(); RequestBufferGuard request_guard; size_t bytes_read = 0U; SetupHttpRequest request{};
    const SetupHttpResult read_result = readRequest(client, request_buffer, sizeof(request_buffer), bytes_read, request);
    if (read_result != SetupHttpResult::Complete) {
        const size_t body_received = bytes_read > request.header_end ? bytes_read - request.header_end : 0U;
        Serial.printf("Panel setup request rejected: reason=%s bytes=%u header=%u body_received=%u body_expected=%u method=%s target=%s\n",
            setupHttpResultName(read_result), static_cast<unsigned>(bytes_read), static_cast<unsigned>(request.header_end),
            static_cast<unsigned>(body_received), static_cast<unsigned>(request.content_length), request.method, request.target);
        send(client, 400, "Bad Request", page("Malformed or oversized request.")); client.stop(); return;
    }
    const SetupHttpRoute route = setupHttpRoute(request);
    if (route == SetupHttpRoute::CaptiveRedirect) { redirectToSetup(client); client.stop(); return; }
    if (route == SetupHttpRoute::SetupPage) {
        send(client, 200, "OK", page()); client.stop(); return;
    }
    if (route != SetupHttpRoute::Save) {
        send(client, 404, "Not Found", page("Not found.")); client.stop(); return;
    }
    Fields fields{};
    if (!parseForm(request_buffer + request.header_end, request.content_length, fields) || std::strcmp(fields.csrf, csrf_token) != 0) {
        send(client, 400, "Bad Request", page("Invalid fields or state token.")); client.stop(); return;
    }
    if (!panelStoreConfig("pending", fields.value)) {
        send(client, 500, "Storage Error", page("Settings could not be saved.")); client.stop(); return;
    }
    rejected_pending_marker = 0U;
    send(client, 200, "OK", page("Saved. The panel will reboot and test the new settings."));
    client.stop(); restart_requested = true;
}
