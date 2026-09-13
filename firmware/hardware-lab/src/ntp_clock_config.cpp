#include "ntp_clock_config.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>

#include <cstddef>
#include <cstring>

#include "hardware_logic.h"

#ifndef SETUP_BUTTON_PIN
#error "SETUP_BUTTON_PIN is required"
#endif

namespace {
constexpr uint32_t kMagic = 0x4e545032U;
constexpr uint16_t kVersion = 1U;
constexpr size_t kMaxHeader = 1024U;
constexpr size_t kMaxBody = 512U;
constexpr size_t kMaxRequest = kMaxHeader + kMaxBody;
constexpr uint32_t kPortalTimeoutMs = 10U * 60U * 1000U;
constexpr uint32_t kRejectedPendingMarker = 0x52504e44U;
constexpr size_t kSetupPasswordCharacters = 8U;
constexpr char kSetupPasswordAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static_assert(sizeof(kSetupPasswordAlphabet) - 1U == 32U, "password alphabet must preserve unbiased five-bit selection");
RTC_DATA_ATTR uint32_t rejected_pending_marker = 0U;
DNSServer dns;
WiFiServer server(80);
bool provisioning = false;
bool restart_requested = false;
uint32_t portal_started_ms = 0U;
char csrf_token[17]{};

uint32_t checksum(const ClockConfig& value) {
    return hardwareConfigCrc32(reinterpret_cast<const uint8_t*>(&value), offsetof(ClockConfig, crc32));
}

bool terminated(const char* value, size_t capacity) { return std::memchr(value, '\0', capacity) != nullptr; }

template <size_t N>
bool copyText(const char* source, size_t length, char (&destination)[N]) {
    if (length >= N) return false;
    std::memcpy(destination, source, length);
    destination[length] = '\0';
    return true;
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool decode(const char* input, size_t length, char* output, size_t capacity) {
    size_t written = 0U;
    for (size_t index = 0U; index < length; ++index) {
        unsigned char value = static_cast<unsigned char>(input[index]);
        if (value == '+') value = ' ';
        else if (value == '%') {
            if (index + 2U >= length) return false;
            const int high = hexValue(input[++index]);
            const int low = hexValue(input[++index]);
            if (high < 0 || low < 0) return false;
            value = static_cast<unsigned char>((high << 4) | low);
        }
        if (value < 0x20U || value > 0x7eU || written + 1U >= capacity) return false;
        output[written++] = static_cast<char>(value);
    }
    output[written] = '\0';
    return true;
}

struct Fields {
    char csrf[17]{};
    char ssid[33]{};
    char password[64]{};
    char timezone[65]{};
    uint8_t seen = 0U;
};

bool assign(Fields& fields, const char* name, size_t name_length, const char* value, size_t value_length) {
    struct Definition { const char* name; uint8_t bit; char* output; size_t capacity; } definitions[] = {
        {"csrf", 1U, fields.csrf, sizeof(fields.csrf)},
        {"wifi_ssid", 2U, fields.ssid, sizeof(fields.ssid)},
        {"wifi_password", 4U, fields.password, sizeof(fields.password)},
        {"timezone", 8U, fields.timezone, sizeof(fields.timezone)},
    };
    for (const auto& definition : definitions) {
        if (std::strlen(definition.name) != name_length || std::memcmp(name, definition.name, name_length) != 0) continue;
        if ((fields.seen & definition.bit) != 0U || !decode(value, value_length, definition.output, definition.capacity)) return false;
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
        if (++count > 4U) return false;
        size_t end = start;
        while (end < length && body[end] != '&') ++end;
        size_t equals = start;
        while (equals < end && body[equals] != '=') ++equals;
        if (equals == start || equals == end || !assign(fields, body + start, equals - start, body + equals + 1U, end - equals - 1U)) return false;
        start = end + 1U;
    }
    return fields.seen == 15U;
}

String page(const char* message = nullptr) {
    String output;
    output.reserve(2400U);
    output += F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><style>body{font:16px system-ui;max-width:36rem;margin:2rem auto;padding:0 1rem;background:#0b1020;color:#e8eefc}main{background:#151d33;padding:1.25rem;border-radius:16px}label{display:block;margin:.8rem 0}input,select{box-sizing:border-box;width:100%;padding:.7rem;background:#0b1020;color:#fff;border:1px solid #3c4a70;border-radius:8px}button{padding:.75rem 1rem;background:#62d6a7;border:0;border-radius:8px;font-weight:700}</style><main><h1>NTP Desk Clock setup</h1><p>Enter Wi-Fi and a POSIX timezone. Settings are tested before replacing the active clock configuration.</p>");
    if (message != nullptr) { output += F("<p><strong>"); output += message; output += F("</strong></p>"); }
    output += F("<form method=post action=/save><input type=hidden name=csrf value='"); output += csrf_token;
    output += F("'><label>Wi-Fi name<input name=wifi_ssid maxlength=32 required></label><label>Wi-Fi password<input type=password name=wifi_password maxlength=63></label>"
                "<label>Timezone<select name=timezone required><option value='CST6CDT,M3.2.0,M11.1.0'>US Central</option>"
                "<option value='EST5EDT,M3.2.0,M11.1.0'>US Eastern</option><option value='MST7MDT,M3.2.0,M11.1.0'>US Mountain</option>"
                "<option value='MST7'>US Arizona</option><option value='PST8PDT,M3.2.0,M11.1.0'>US Pacific</option>"
                "<option value='AKST9AKDT,M3.2.0,M11.1.0'>US Alaska</option><option value='HST10'>US Hawaii</option>"
                "<option value='UTC0'>UTC</option><option value='GMT0BST,M3.5.0/1,M10.5.0'>United Kingdom</option>"
                "<option value='CET-1CEST,M3.5.0,M10.5.0/3'>Central Europe</option><option value='AEST-10AEDT,M10.1.0,M4.1.0/3'>Australia Eastern</option>"
                "<option value='JST-9'>Japan</option><option value='IST-5:30'>India</option></select></label><button>Save and test</button></form></main>");
    return output;
}

void send(WiFiClient& client, int status, const char* reason, const String& body) {
    client.printf("HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n", status, reason, static_cast<unsigned>(body.length()));
    client.print(body);
}

bool candidateFrom(const Fields& fields, ClockConfig& value) {
    value = {};
    value.magic = kMagic;
    value.version = kVersion;
    if (!copyText(fields.ssid, std::strlen(fields.ssid), value.wifi_ssid) ||
        !copyText(fields.password, std::strlen(fields.password), value.wifi_password) ||
        !copyText(fields.timezone, std::strlen(fields.timezone), value.timezone)) return false;
    value.crc32 = checksum(value);
    return clockConfigValid(value);
}

bool readRequest(WiFiClient& client, char* request, size_t capacity, size_t& header_end, size_t& content_length,
                 char* method, size_t method_capacity, char* target, size_t target_capacity) {
    size_t used = 0U;
    header_end = 0U;
    content_length = 0U;
    const uint32_t started = millis();
    while (client.connected() && static_cast<uint32_t>(millis() - started) < 2000U) {
        while (client.available()) {
            if (used + 1U >= capacity) return false;
            request[used++] = static_cast<char>(client.read()); request[used] = '\0';
            if (header_end == 0U && used >= 4U && std::memcmp(request + used - 4U, "\r\n\r\n", 4U) == 0) {
                header_end = used;
                if (header_end > kMaxHeader) return false;
                bool has_content_length = false;
                if (!hardwareParseHttpRequest(request, header_end, kMaxBody, method, method_capacity, target, target_capacity, content_length, has_content_length)) return false;
            }
            if (header_end != 0U && used >= header_end + content_length) return true;
        }
        delay(1);
    }
    return false;
}

void randomHex(char* output, size_t bytes) {
    static constexpr char hex[] = "0123456789abcdef";
    for (size_t index = 0U; index < bytes; ++index) {
        const uint8_t value = static_cast<uint8_t>(esp_random());
        output[index * 2U] = hex[value >> 4U]; output[index * 2U + 1U] = hex[value & 15U];
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

bool clockConfigValid(const ClockConfig& value) {
    return value.magic == kMagic && value.version == kVersion && value.crc32 == checksum(value) &&
           terminated(value.wifi_ssid, sizeof(value.wifi_ssid)) && terminated(value.wifi_password, sizeof(value.wifi_password)) &&
           terminated(value.timezone, sizeof(value.timezone)) && validWifiSsid(value.wifi_ssid) &&
           validWifiPassword(value.wifi_password) && validPosixTimezone(value.timezone);
}

bool clockLoadConfig(const char* key, ClockConfig& value) {
    Preferences preferences;
    if (!preferences.begin("ntpclock", true)) return false;
    const size_t length = preferences.getBytesLength(key);
    const size_t read = length == sizeof(value) ? preferences.getBytes(key, &value, sizeof(value)) : 0U;
    preferences.end();
    return read == sizeof(value) && clockConfigValid(value);
}

bool clockStoreConfig(const char* key, const ClockConfig& value) {
    Preferences preferences;
    if (!clockConfigValid(value) || !preferences.begin("ntpclock", false)) return false;
    const bool stored = preferences.putBytes(key, &value, sizeof(value)) == sizeof(value);
    preferences.end();
    if (!stored) return false;
    ClockConfig verified{};
    return clockLoadConfig(key, verified) && std::memcmp(&verified, &value, sizeof(value)) == 0;
}

bool clockRemoveConfig(const char* key) {
    Preferences preferences;
    if (!preferences.begin("ntpclock", false)) return false;
    const bool removed = !preferences.isKey(key) || preferences.remove(key);
    preferences.end();
    return removed;
}

bool clockPromotePending(const ClockConfig& value) {
    if (!clockStoreConfig("active", value)) return false;
    rejected_pending_marker = clockRemoveConfig("pending") ? 0U : kRejectedPendingMarker;
    return true;
}

bool clockPendingSuppressed() { return rejected_pending_marker == kRejectedPendingMarker; }

bool clockRejectPending() {
    const bool removed = clockRemoveConfig("pending");
    rejected_pending_marker = removed ? 0U : kRejectedPendingMarker;
    return removed;
}

bool clockRecoveryRequested() {
    Serial.println("Hold BOOT now for two seconds to open setup");
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

bool clockStartProvisioning() {
    uint8_t mac[6]{}; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char ssid[32]; char password[kSetupPasswordCharacters + 1U];
    std::snprintf(ssid, sizeof(ssid), "NTP-Clock-Setup-%02X%02X%02X", mac[3], mac[4], mac[5]);
    WiFi.mode(WIFI_AP);
    makeReadableSetupPassword(password); randomHex(csrf_token, 8U);
    if (!WiFi.softAP(ssid, password, 1, false, 1)) return false;
    if (!dns.start(53, "*", WiFi.softAPIP())) { WiFi.softAPdisconnect(true); return false; }
    server.begin(); provisioning = true; portal_started_ms = millis();
    Serial.printf("Provisioning: join %s with password %s, then open http://%s\n", ssid, password, WiFi.softAPIP().toString().c_str());
    return true;
}

bool clockProvisioningActive() { return provisioning; }

void clockHandleProvisioning() {
    if (!provisioning) return;
    dns.processNextRequest();
    WiFiClient client = server.accept();
    if (client) {
        char request[kMaxRequest + 1U]{}; char method[8]{}; char target[64]{}; size_t header_end = 0U; size_t content_length = 0U;
        if (!readRequest(client, request, sizeof(request), header_end, content_length, method, sizeof(method), target, sizeof(target))) {
            send(client, 413, "Payload Too Large", page("Request rejected."));
        } else if (std::strcmp(method, "GET") == 0) {
            send(client, 200, "OK", page());
        } else if (std::strcmp(method, "POST") == 0 && std::strcmp(target, "/save") == 0 && content_length > 0U) {
            Fields fields{}; ClockConfig candidate{};
            if (!parseForm(request + header_end, content_length, fields) || std::strcmp(fields.csrf, csrf_token) != 0 || !candidateFrom(fields, candidate)) {
                send(client, 400, "Bad Request", page("Check the Wi-Fi and POSIX timezone values."));
            } else if (!clockStoreConfig("pending", candidate)) {
                send(client, 503, "Unavailable", page("NVS write or verification failed."));
            } else {
                rejected_pending_marker = 0U;
                send(client, 200, "OK", page("Settings saved. Restarting to verify NTP.")); restart_requested = true;
            }
        } else send(client, 405, "Method Not Allowed", page());
        delay(1); client.stop();
    }
    if (restart_requested || static_cast<uint32_t>(millis() - portal_started_ms) >= kPortalTimeoutMs) { delay(250); ESP.restart(); }
}
