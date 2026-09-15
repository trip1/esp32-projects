#include "dashboard_config.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <nvs.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

#ifndef SETUP_BUTTON_PIN
#error "SETUP_BUTTON_PIN is required"
#endif

namespace {
constexpr uint32_t kMagic = 0x4c444153U;
constexpr uint16_t kVersion = 4U;
constexpr size_t kMaxHeader = 1024U;
constexpr size_t kMaxBody = 3072U;
constexpr size_t kMaxRequest = kMaxHeader + kMaxBody;
constexpr uint32_t kPortalTimeoutMs = 10U * 60U * 1000U;
constexpr uint32_t kRejectedPendingMarker = 0x44415358U;
constexpr char kPasswordAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

struct StoredConfig { uint32_t magic; uint16_t version; uint16_t reserved; DashboardConfig value; uint32_t crc32; };
struct LegacyScheduleV2 { uint8_t order[4]{}; uint16_t duration_seconds[4]{}; };
struct LegacyConfigV2 { PanelConfig sources{}; char timezone[65]{}; LegacyScheduleV2 schedule{}; };
struct LegacyStoredConfigV2 { uint32_t magic; uint16_t version; uint16_t reserved; LegacyConfigV2 value; uint32_t crc32; };
struct LegacyScheduleV3 { uint8_t order[10]{}; uint16_t duration_seconds[10]{}; };
struct LegacyConfigV3 { PanelConfig sources{}; char timezone[65]{}; LegacyScheduleV3 schedule{}; };
struct LegacyStoredConfigV3 { uint32_t magic; uint16_t version; uint16_t reserved; LegacyConfigV3 value; uint32_t crc32; };
struct Fields {
    DashboardConfig value{};
    char csrf[17]{};
    char order[kDashboardScreenCount][12]{};
    char duration[kDashboardScreenCount][5]{};
    uint64_t seen = 0U;
};

RTC_DATA_ATTR uint32_t rejected_pending_marker = 0U;
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

uint32_t checksum(const StoredConfig& value) { return panelCrc32(reinterpret_cast<const unsigned char*>(&value), offsetof(StoredConfig, crc32)); }

bool decode(const char* input, size_t length, char* output, size_t capacity) {
    if (length >= capacity) return false;
    size_t written = 0U;
    auto hex = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t index = 0U; index < length; ++index) {
        unsigned char value = static_cast<unsigned char>(input[index]);
        if (value == '+') value = ' ';
        else if (value == '%') {
            if (index + 2U >= length) return false;
            const int high = hex(input[++index]); const int low = hex(input[++index]);
            if (high < 0 || low < 0) return false;
            value = static_cast<unsigned char>((high << 4) | low);
        }
        if (value < 0x20U || value > 0x7eU || written + 1U >= capacity) return false;
        output[written++] = static_cast<char>(value);
    }
    output[written] = '\0';
    return true;
}

bool assign(Fields& fields, const char* name, size_t name_length, const char* value, size_t value_length) {
    struct Definition { const char* name; uint32_t bit; char* output; size_t capacity; } definitions[] = {
        {"csrf",1U<<0,fields.csrf,sizeof(fields.csrf)}, {"wifi_ssid",1U<<1,fields.value.sources.wifi_ssid,sizeof(fields.value.sources.wifi_ssid)},
        {"wifi_password",1U<<2,fields.value.sources.wifi_password,sizeof(fields.value.sources.wifi_password)},
        {"latitude",1U<<3,fields.value.sources.latitude,sizeof(fields.value.sources.latitude)}, {"longitude",1U<<4,fields.value.sources.longitude,sizeof(fields.value.sources.longitude)},
        {"timezone",1U<<5,fields.value.timezone,sizeof(fields.value.timezone)},
    };
    for (const auto& definition : definitions) {
        if (std::strlen(definition.name) != name_length || std::memcmp(name, definition.name, name_length) != 0) continue;
        if ((fields.seen & definition.bit) != 0U || !decode(value, value_length, definition.output, definition.capacity)) return false;
        fields.seen |= definition.bit; return true;
    }
    for (size_t index = 0U; index < kDashboardScreenCount; ++index) {
        char order_name[9]{}; char duration_name[24]{};
        std::snprintf(order_name, sizeof(order_name), "order_%u", static_cast<unsigned>(index + 1U));
        std::snprintf(duration_name, sizeof(duration_name), "duration_%s", dashboardScreenName(static_cast<DashboardScreen>(index)));
        const uint64_t order_bit = 1ULL << (6U + index); const uint64_t duration_bit = 1ULL << (15U + index);
        if (std::strlen(order_name) == name_length && std::memcmp(name, order_name, name_length) == 0) {
            if ((fields.seen & order_bit) != 0U || !decode(value, value_length, fields.order[index], sizeof(fields.order[index]))) return false;
            fields.seen |= order_bit; return true;
        }
        if (std::strlen(duration_name) == name_length && std::memcmp(name, duration_name, name_length) == 0) {
            if ((fields.seen & duration_bit) != 0U || !decode(value, value_length, fields.duration[index], sizeof(fields.duration[index]))) return false;
            fields.seen |= duration_bit; return true;
        }
    }
    return false;
}

bool parseForm(const char* body, size_t length, Fields& fields) {
    if (body == nullptr || length == 0U || length > kMaxBody) return false;
    size_t start = 0U; unsigned count = 0U;
    while (start < length) {
        if (++count > 24U) return false;
        size_t end = start; while (end < length && body[end] != '&') ++end;
        size_t equals = start; while (equals < end && body[equals] != '=') ++equals;
        if (equals == start || equals == end || !assign(fields, body + start, equals - start, body + equals + 1U, end - equals - 1U)) return false;
        start = end + 1U;
    }
    if (fields.seen != 0xffffffULL) return false;
    for (size_t index = 0U; index < kDashboardScreenCount; ++index) {
        if (!dashboardParseScreen(fields.order[index], fields.value.schedule.order[index])) return false;
        unsigned duration = 0U;
        for (const char* digit = fields.duration[index]; *digit != '\0'; ++digit) { if (*digit < '0' || *digit > '9') return false; duration = duration * 10U + static_cast<unsigned>(*digit - '0'); }
        fields.value.schedule.duration_seconds[index] = static_cast<uint16_t>(duration);
    }
    return true;
}

void appendEscaped(String& output, const char* value) {
    if (value == nullptr) return;
    for (; *value != '\0'; ++value) {
        switch (*value) { case '&': output += F("&amp;"); break; case '<': output += F("&lt;"); break; case '>': output += F("&gt;"); break; case '"': output += F("&quot;"); break; case '\'': output += F("&#39;"); break; default: output += *value; }
    }
}

void addScreenOptions(String& output, size_t selected) {
    static constexpr const char* values[] = {"clock", "satellite", "weather", "launch", "moon", "solar", "planet", "neo", "deep-space"};
    static constexpr const char* labels[] = {"Clock", "ISS", "Weather", "Next Launch", "Moon", "Solar Activity", "Planet Visibility", "Near-Earth Object", "Deep-Space Mission"};
    for (size_t index = 0U; index < kDashboardScreenCount; ++index) {
        output += F("<option value="); output += values[index];
        if (index == selected) output += F(" selected");
        output += '>'; output += labels[index]; output += F("</option>");
    }
}

void addTextInput(String& output, const char* label, const char* name, size_t maximum, const char* value, bool required = true) {
    output += F("<label>"); output += label; output += F("<input name="); output += name; output += F(" maxlength="); output += String(maximum);
    if (required) output += F(" required");
    output += F(" value=\""); appendEscaped(output, value); output += F("\"></label>");
}

String page(const char* message = nullptr, const DashboardConfig* preset = nullptr) {
    String output; output.reserve(16000U);
    output += F("<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width'><title>LCD1602 Smart Dashboard setup</title><style>body{font:16px system-ui;max-width:44rem;margin:2rem auto;padding:0 1rem;background:#0b1020;color:#e8eefc}main,fieldset{background:#151d33;padding:1rem;border-radius:14px;border:1px solid #354263;margin:1rem 0}label{display:block;margin:.65rem 0}input,select{box-sizing:border-box;width:100%;padding:.65rem;background:#0b1020;color:#fff;border:1px solid #52638f;border-radius:8px}.row{display:grid;grid-template-columns:2fr 1fr;gap:.75rem}button{padding:.8rem 1rem;background:#62d6a7;border:0;border-radius:8px;font-weight:700}</style></head><body><main><h1>LCD1602 Smart Dashboard</h1><p>Configure all nine screens, their order, and display duration. Space screens use your weather coordinates with the DS9 launch tracker bridge. Settings are tested before promotion.</p>");
    if (message) { output += F("<p role=alert><strong>"); appendEscaped(output, message); output += F("</strong></p>"); }
    output += F("<form method=post action=/save><input type=hidden name=csrf value='"); output += csrf_token;
    output += F("'><fieldset><legend>Network and clock</legend>");
    addTextInput(output,"Wi-Fi name","wifi_ssid",32U,preset?preset->sources.wifi_ssid:"");
    output += F("<label>Wi-Fi password<input type=password autocomplete=new-password name=wifi_password maxlength=63 placeholder='Leave blank to keep current'></label><label>Timezone<select name=timezone required>");
    static constexpr const char* zones[] = {"CST6CDT,M3.2.0,M11.1.0","EST5EDT,M3.2.0,M11.1.0","MST7MDT,M3.2.0,M11.1.0","MST7","PST8PDT,M3.2.0,M11.1.0","AKST9AKDT,M3.2.0,M11.1.0","HST10","UTC0","GMT0BST,M3.5.0/1,M10.5.0","CET-1CEST,M3.5.0,M10.5.0/3","AEST-10AEDT,M10.1.0,M4.1.0/3","JST-9","IST-5:30"};
    static constexpr const char* zone_labels[] = {"US Central","US Eastern","US Mountain","US Arizona","US Pacific","US Alaska","US Hawaii","UTC","United Kingdom","Central Europe","Australia Eastern","Japan","India"};
    for(size_t i=0U;i<13U;++i){output+=F("<option value=\"");appendEscaped(output,zones[i]);output+='"';if(preset&&std::strcmp(preset->timezone,zones[i])==0)output+=F(" selected");output+='>';output+=zone_labels[i];output+=F("</option>");}
    output += F("</select></label></fieldset><fieldset><legend>Weather and sky location</legend>");addTextInput(output,"Latitude","latitude",16U,preset?preset->sources.latitude:"");addTextInput(output,"Longitude","longitude",16U,preset?preset->sources.longitude:"");output += F("</fieldset><fieldset><legend>Screen rotation</legend>");
    for (size_t index = 0U; index < kDashboardScreenCount; ++index) {
        const size_t selected=preset?static_cast<size_t>(preset->schedule.order[index]):index;output += F("<label>Position "); output += String(index + 1U); output += F("<select name=order_"); output += String(index + 1U); output += F(" required>"); addScreenOptions(output, selected); output += F("</select></label>");
    }
    output += F("<h2>Duration for each screen</h2>");
    static constexpr const char* duration_labels[] = {"Clock", "ISS", "Weather", "Next Launch", "Moon", "Solar Activity", "Planet Visibility", "Near-Earth Object", "Deep-Space Mission"};
    for (size_t index = 0U; index < kDashboardScreenCount; ++index) {
        output += F("<label>"); output += duration_labels[index]; output += F(" seconds<input type=number min=5 max=3600 name=duration_");
        output += dashboardScreenName(static_cast<DashboardScreen>(index)); output += F(" value="); output += String(preset?preset->schedule.duration_seconds[index]:15U); output += F(" required></label>");
    }
    output += F("</fieldset><button>Save and test</button></form></main></body></html>"); return output;
}

void send(WiFiClient& client, int status, const char* reason, const String& body) {
    client.printf("HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; form-action 'self'\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n\r\n", status, reason, static_cast<unsigned>(body.length())); client.print(body);
}

bool readRequest(WiFiClient& client, char* request, size_t capacity, size_t& header_end, size_t& content_length, char* method, size_t method_capacity, char* target, size_t target_capacity) {
    size_t used = 0U; header_end = 0U; content_length = 0U; const uint32_t started = millis();
    while (client.connected() && static_cast<uint32_t>(millis() - started) < 2000U) {
        while (client.available()) {
            if (used + 1U >= capacity) return false;
            request[used++] = static_cast<char>(client.read()); request[used] = '\0';
            if (header_end == 0U && used >= 4U && std::memcmp(request + used - 4U, "\r\n\r\n", 4U) == 0) {
                header_end = used; if (header_end > kMaxHeader) return false; bool has_length = false;
                if (!panelParseHttpRequest(request, header_end, kMaxBody, method, method_capacity, target, target_capacity, content_length, has_length)) return false;
                if (std::strcmp(method, "POST") == 0 && !has_length) return false;
            }
            if (header_end != 0U && used >= header_end + content_length) return used == header_end + content_length;
        }
        delay(1);
    }
    return false;
}

void randomHex(char* output, size_t bytes) { static constexpr char hex[] = "0123456789abcdef"; for (size_t i=0;i<bytes;++i) { const uint8_t v=static_cast<uint8_t>(esp_random()); output[i*2U]=hex[v>>4U]; output[i*2U+1U]=hex[v&15U]; } output[bytes*2U]='\0'; }
void makePassword(char (&output)[9]) { for (size_t i=0;i<8U;++i) output[i]=kPasswordAlphabet[esp_random()&31U]; output[8]='\0'; }

bool loadExact(const char* key, DashboardConfig& value) {
    Preferences preferences; if (!preferences.begin("lcddash", true)) return false; StoredConfig stored{};
    const size_t length=preferences.getBytesLength(key); const size_t read=length==sizeof(stored)?preferences.getBytes(key,&stored,sizeof(stored)):0U; preferences.end();
    if (read!=sizeof(stored)||stored.magic!=kMagic||stored.version!=kVersion||stored.reserved!=0U||stored.crc32!=checksum(stored)||!dashboardConfigValid(stored.value)) return false;
    value=stored.value; return true;
}

void clearMqtt(PanelConfig& value) {
    std::memset(value.mqtt_host,0,sizeof(value.mqtt_host));value.mqtt_port=0U;
    std::memset(value.mqtt_username,0,sizeof(value.mqtt_username));std::memset(value.mqtt_password,0,sizeof(value.mqtt_password));
    std::memset(value.mqtt_topic,0,sizeof(value.mqtt_topic));std::memset(value.label,0,sizeof(value.label));
}

bool loadLegacyV3(const char* key, DashboardConfig& value) {
    Preferences preferences; if (!preferences.begin("lcddash", true)) return false; LegacyStoredConfigV3 stored{};
    const size_t length=preferences.getBytesLength(key); const size_t read=length==sizeof(stored)?preferences.getBytes(key,&stored,sizeof(stored)):0U; preferences.end();
    const uint32_t crc=panelCrc32(reinterpret_cast<const unsigned char*>(&stored),offsetof(LegacyStoredConfigV3,crc32));
    if(read!=sizeof(stored)||stored.magic!=kMagic||stored.version!=3U||stored.reserved!=0U||stored.crc32!=crc)return false;
    value.sources=stored.value.sources;clearMqtt(value.sources);std::memcpy(value.timezone,stored.value.timezone,sizeof(value.timezone));
    return dashboardMigrateLegacySchedule(stored.value.schedule.order,stored.value.schedule.duration_seconds,10U,value.schedule)&&dashboardConfigValid(value);
}

bool loadLegacyV2(const char* key, DashboardConfig& value) {
    Preferences preferences; if (!preferences.begin("lcddash", true)) return false; LegacyStoredConfigV2 stored{};
    const size_t length=preferences.getBytesLength(key); const size_t read=length==sizeof(stored)?preferences.getBytes(key,&stored,sizeof(stored)):0U; preferences.end();
    const uint32_t crc=panelCrc32(reinterpret_cast<const unsigned char*>(&stored),offsetof(LegacyStoredConfigV2,crc32));
    if(read!=sizeof(stored)||stored.magic!=kMagic||stored.version!=2U||stored.reserved!=0U||stored.crc32!=crc)return false;
    value.sources=stored.value.sources;clearMqtt(value.sources);std::memcpy(value.timezone,stored.value.timezone,sizeof(value.timezone));
    return dashboardMigrateLegacySchedule(stored.value.schedule.order,stored.value.schedule.duration_seconds,4U,value.schedule)&&dashboardConfigValid(value);
}

bool loadLegacy(const char* key, DashboardConfig& value) { return loadLegacyV3(key,value)||loadLegacyV2(key,value); }

bool loadAny(const char* key, DashboardConfig& value) {
    if(loadExact(key,value))return true;
    DashboardConfig migrated{};if(!loadLegacy(key,migrated))return false;
    if(std::strcmp(key,"active")==0){
        DashboardConfig legacy_backup{};DashboardConfig current_backup{};
        if(loadLegacy("backup",legacy_backup)){if(!dashboardStoreConfig("backup", legacy_backup))return false;}
        else if(!loadExact("backup",current_backup)&&!dashboardStoreConfig("backup",migrated))return false;
    }
    if(!dashboardStoreConfig(key, migrated))return false;
    value=migrated;return true;
}

bool loadForceBackupMarker(uint32_t& marker) {
    nvs_handle_t handle = 0;
    if (nvs_open("lcddash", NVS_READONLY, &handle) != ESP_OK) return false;
    const esp_err_t status = nvs_get_u32(handle, "forcebak", &marker);
    nvs_close(handle);
    if (status == ESP_ERR_NVS_NOT_FOUND) { marker = 0U; return true; }
    return status == ESP_OK && marker == kRejectedPendingMarker;
}

bool storeForceBackupMarker(bool enabled) {
    nvs_handle_t handle = 0;
    if (nvs_open("lcddash", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t status = enabled ? nvs_set_u32(handle, "forcebak", kRejectedPendingMarker) : nvs_erase_key(handle, "forcebak");
    if (!enabled && status == ESP_ERR_NVS_NOT_FOUND) status = ESP_OK;
    if (status == ESP_OK) status = nvs_commit(handle);
    uint32_t check = 0U;
    const esp_err_t read_status = status == ESP_OK ? nvs_get_u32(handle, "forcebak", &check) : status;
    nvs_close(handle);
    return enabled ? read_status == ESP_OK && check == kRejectedPendingMarker : read_status == ESP_ERR_NVS_NOT_FOUND;
}
}  // namespace

bool dashboardConfigValid(const DashboardConfig& value) {
    return dashboardTimezoneValid(value.timezone) && dashboardScheduleValid(value.schedule)
        && panelConfigValid(PanelKind::Weather, value.sources) && panelConfigValid(PanelKind::Space, value.sources);
}

const char* dashboardValidationError(const DashboardConfig& value) {
    if (!panelConfigValid(PanelKind::Space, value.sources)) return "Check the Wi-Fi name and password (passwords must be blank or at least 8 characters).";
    if (!dashboardTimezoneValid(value.timezone)) return "Select a supported timezone.";
    if (!dashboardScheduleValid(value.schedule)) return "Check the screen order and ensure every screen appears once with durations from 5 to 3600 seconds.";
    if (!panelConfigValid(PanelKind::Weather, value.sources)) return "Check the weather latitude and longitude.";
    return nullptr;
}

bool dashboardLoadConfig(const char* key, DashboardConfig& value) {
    if (std::strcmp(key, "active") == 0) {
        uint32_t force_backup_marker = 0U;
        if (!loadForceBackupMarker(force_backup_marker)) return false;
        if (force_backup_marker == kRejectedPendingMarker) {
            if (!loadAny("backup", value) || !dashboardRemoveConfig("active")) return false;
            return true;
        }
        if (loadAny("active", value)) return true;
        if (!dashboardRemoveConfig("active")) return false;
        return loadAny("backup", value);
    }
    return loadAny(key, value);
}
bool dashboardStoreConfig(const char* key, const DashboardConfig& value) {
    if (!dashboardConfigValid(value)) return false;
    const bool writing_active = std::strcmp(key, "active") == 0;
    if (writing_active && !storeForceBackupMarker(true)) return false;
    StoredConfig stored{kMagic,kVersion,0U,value,0U}; stored.crc32=checksum(stored);
    Preferences preferences; if (!preferences.begin("lcddash",false)) return false; const bool ok=preferences.putBytes(key,&stored,sizeof(stored))==sizeof(stored); preferences.end();
    DashboardConfig check{};
    if (!ok || !loadExact(key, check) || std::memcmp(&check, &value, sizeof(value)) != 0) {
        if (writing_active) dashboardRemoveConfig("active");
        else if (!dashboardRemoveConfig(key)) rejected_pending_marker = kRejectedPendingMarker;
        return false;
    }
    return !writing_active || storeForceBackupMarker(false);
}
bool dashboardRemoveConfig(const char* key) { nvs_handle_t handle=0; if(nvs_open("lcddash",NVS_READWRITE,&handle)!=ESP_OK)return false; size_t length=0U; const esp_err_t status=nvs_get_blob(handle,key,nullptr,&length); if(status==ESP_ERR_NVS_NOT_FOUND){nvs_close(handle);return true;} if(status!=ESP_OK||nvs_erase_key(handle,key)!=ESP_OK||nvs_commit(handle)!=ESP_OK){nvs_close(handle);return false;} length=0U; const bool absent=nvs_get_blob(handle,key,nullptr,&length)==ESP_ERR_NVS_NOT_FOUND; nvs_close(handle); return absent; }
bool dashboardPromotePending(const DashboardConfig& value) { DashboardConfig previous{}; if(dashboardLoadConfig("active",previous)&&!dashboardStoreConfig("backup",previous))return false;if(!dashboardStoreConfig("active",value))return false;rejected_pending_marker=dashboardRemoveConfig("pending")?0U:kRejectedPendingMarker;return true; }
bool dashboardPendingSuppressed(){return rejected_pending_marker==kRejectedPendingMarker;} void dashboardBeginPendingValidation(){rejected_pending_marker=kRejectedPendingMarker;} void dashboardRejectPending(){rejected_pending_marker=dashboardRemoveConfig("pending")?0U:kRejectedPendingMarker;}
bool dashboardRecoveryRequested(){Serial.println("Hold BOOT now for two seconds to open dashboard setup");const uint32_t started=millis();uint32_t held=0U;while(static_cast<uint32_t>(millis()-started)<5000U){if(digitalRead(SETUP_BUTTON_PIN)==LOW){if(held==0U)held=millis();if(static_cast<uint32_t>(millis()-held)>=2000U)return true;}else held=0U;delay(10);}return false;}
bool dashboardStartProvisioning(){uint8_t mac[6]{};esp_read_mac(mac,ESP_MAC_WIFI_STA);char ssid[32]{};char password[9]{};std::snprintf(ssid,sizeof(ssid),"LCD-Dashboard-%02X%02X%02X",mac[3],mac[4],mac[5]);WiFi.mode(WIFI_AP);makePassword(password);randomHex(csrf_token,8U);if(!WiFi.softAP(ssid,password,1,false,1))return false;if(!dns.start(53,"*",WiFi.softAPIP())){WiFi.softAPdisconnect(true);return false;}server.begin();if(!server){dns.stop();WiFi.softAPdisconnect(true);return false;}portal_started_ms=millis();provisioning=true;Serial.printf("Dashboard setup network: %s\nSetup password: %s\nOpen http://192.168.4.1/\n",ssid,password);return true;}
bool dashboardProvisioningActive(){return provisioning;}
void dashboardHandleProvisioning() {
    if (!provisioning) return;
    dns.processNextRequest();
    if (static_cast<uint32_t>(millis() - portal_started_ms) >= kPortalTimeoutMs) { ESP.restart(); return; }
    if (restart_requested) { delay(200); ESP.restart(); }
    WiFiClient client = server.accept();
    if (!client) return;
    client.setTimeout(2U);
    clearRequestBuffer(); RequestBufferGuard request_guard; char method[8]{}; char target[64]{};
    size_t header_end = 0U; size_t content_length = 0U;
    DashboardConfig existing{};
    const bool has_existing = dashboardLoadConfig("active", existing);
    if (!readRequest(client, request_buffer, sizeof(request_buffer), header_end, content_length, method, sizeof(method), target, sizeof(target))) {
        send(client, 400, "Bad Request", page("Malformed or oversized request.", has_existing ? &existing : nullptr)); client.stop(); return;
    }
    if (std::strcmp(method, "GET") == 0 && std::strcmp(target, "/") == 0) {
        send(client, 200, "OK", page(nullptr, has_existing ? &existing : nullptr)); client.stop(); return;
    }
    if (std::strcmp(method, "POST") != 0 || std::strcmp(target, "/save") != 0) {
        send(client, 404, "Not Found", page("Not found.", has_existing ? &existing : nullptr)); client.stop(); return;
    }
    Fields fields{};
    const bool parsed = parseForm(request_buffer + header_end, content_length, fields);
    if (parsed && has_existing) {
        if (fields.value.sources.wifi_password[0] == '\0') std::memcpy(fields.value.sources.wifi_password, existing.sources.wifi_password, sizeof(fields.value.sources.wifi_password));
    }
    if (!parsed) {
        send(client, 400, "Bad Request", page("The request is incomplete or contains an invalid field.", has_existing ? &existing : nullptr)); client.stop(); return;
    }
    if (std::strcmp(fields.csrf, csrf_token) != 0) {
        send(client, 400, "Bad Request", page("The setup session expired. Reload the page and try again.", &fields.value)); client.stop(); return;
    }
    if (const char* validation_error = dashboardValidationError(fields.value)) {
        send(client, 400, "Bad Request", page(validation_error, &fields.value)); client.stop(); return;
    }
    rejected_pending_marker = kRejectedPendingMarker;
    if (!dashboardStoreConfig("pending", fields.value)) {
        send(client, 500, "Storage Error", page("Settings could not be saved.", &fields.value)); client.stop(); return;
    }
    rejected_pending_marker = 0U;
    send(client, 200, "OK", page("Saved. The dashboard will reboot and verify Wi-Fi, time, and weather.", &fields.value));
    client.stop(); restart_requested = true;
}
