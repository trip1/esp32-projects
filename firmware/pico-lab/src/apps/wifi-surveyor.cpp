#include <Arduino.h>
#include <WiFi.h>

#include <array>
#include <string_view>

#include "pico/rand.h"
#include "pico_logic.h"

namespace {

constexpr char AP_NAME[] = "Pico-W-Surveyor";
constexpr std::size_t kSetupPasswordCharacters = 8U;
constexpr char kSetupPasswordAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static_assert(sizeof(kSetupPasswordAlphabet) - 1U == 32U, "password alphabet must preserve unbiased five-bit selection");
constexpr std::size_t MAX_NETWORKS = 20;
constexpr std::size_t MAX_REQUEST_BYTES = 1024;
constexpr uint32_t REQUEST_TIMEOUT_MS = 1500;

struct Network {
    String ssid;
    int32_t rssi = -100;
    uint8_t channel = 0;
};

std::array<Network, MAX_NETWORKS> networks{};
std::size_t network_count = 0;
WiFiServer server(80);
char ap_password[kSetupPasswordCharacters + 1U]{};
uint32_t last_serial_reminder = 0;
bool access_ready = false;

void makeReadableSetupPassword() {
    uint64_t random_bits = get_rand_64();
    for (std::size_t index = 0; index < kSetupPasswordCharacters; ++index) {
        ap_password[index] = kSetupPasswordAlphabet[random_bits & 31U];
        random_bits >>= 5U;
    }
    ap_password[kSetupPasswordCharacters] = '\0';
}

void captureSurvey() {
    WiFi.mode(WIFI_STA);
    const int found = WiFi.scanNetworks();
    if (found > 0) {
        network_count = min(static_cast<std::size_t>(found), MAX_NETWORKS);
        for (std::size_t index = 0; index < network_count; ++index) {
            const char* ssid = WiFi.SSID(static_cast<uint8_t>(index));
            networks[index].ssid = String(ssid == nullptr ? "" : ssid).substring(0, 32);
            networks[index].rssi = WiFi.RSSI(static_cast<uint8_t>(index));
            networks[index].channel = WiFi.channel(static_cast<uint8_t>(index));
        }
    }
    WiFi.scanDelete();
    WiFi.disconnect();
}

String page() {
    String html;
    html.reserve(6400);
    html += F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Pico W Surveyor</title><style>body{font:16px system-ui;background:#10171b;color:#eef4f6;max-width:760px;margin:0 auto;padding:24px}table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:10px;border-bottom:1px solid #34434b}small{color:#aebdc4}.bar{height:8px;background:#29404b;border-radius:8px}.bar i{display:block;height:100%;background:#59d2a9;border-radius:8px}</style></head><body><h1>Pico W Wi-Fi Surveyor</h1><p><small>Read-only boot-time scan. Reboot the board to refresh. Results are capped at 20 networks.</small></p><table><thead><tr><th>SSID</th><th>Channel</th><th>RSSI</th><th>Signal</th></tr></thead><tbody>");
    if (network_count == 0) {
        html += F("<tr><td colspan=4>No networks found during the boot scan.</td></tr>");
    }
    for (std::size_t index = 0; index < network_count; ++index) {
        html += F("<tr><td>");
        html += pico_lab::escapeHtml(networks[index].ssid.c_str(), 32).c_str();
        html += F("</td><td>");
        html += networks[index].channel;
        html += F("</td><td>");
        html += networks[index].rssi;
        html += F(" dBm</td><td><div class=bar><i style='width:");
        html += pico_lab::signalPercent(networks[index].rssi);
        html += F("%'></i></div></td></tr>");
    }
    html += F("</tbody></table></body></html>");
    return html;
}

void sendResponse(WiFiClient& client, int status, const char* reason, const char* type, const String& body) {
    client.print(F("HTTP/1.1 "));
    client.print(status);
    client.print(' ');
    client.print(reason);
    client.print(F("\r\nContent-Type: "));
    client.print(type);
    client.print(F("\r\nContent-Length: "));
    client.print(body.length());
    client.print(F("\r\nCache-Control: no-store\r\nConnection: close\r\n"));
    client.print(F("Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; frame-ancestors 'none'\r\n"));
    client.print(F("X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n\r\n"));
    client.print(body);
}

void handleClient() {
    WiFiClient client = server.accept();
    if (!client) {
        return;
    }
    client.setNoDelay(true);
    std::array<char, MAX_REQUEST_BYTES + 1> request{};
    std::size_t length = 0;
    bool complete = false;
    const uint32_t started = millis();
    while (client.connected() && millis() - started < REQUEST_TIMEOUT_MS) {
        while (client.available() && length < MAX_REQUEST_BYTES) {
            request[length++] = static_cast<char>(client.read());
            if (length >= 4 && request[length - 4] == '\r' && request[length - 3] == '\n'
                && request[length - 2] == '\r' && request[length - 1] == '\n') {
                complete = true;
                break;
            }
        }
        if (complete || length == MAX_REQUEST_BYTES) {
            break;
        }
        delay(1);
    }

    if (length == MAX_REQUEST_BYTES && !complete) {
        sendResponse(client, 431, "Request Header Fields Too Large", "text/plain; charset=utf-8", String("Request headers too large\n"));
    } else if (!complete) {
        sendResponse(client, 408, "Request Timeout", "text/plain; charset=utf-8", String("Request timeout\n"));
    } else {
        switch (pico_lab::parseSurveyRequest(std::string_view(request.data(), length))) {
            case pico_lab::SurveyRequestResult::Ok: {
                const String body = page();
                sendResponse(client, 200, "OK", "text/html; charset=utf-8", body);
                break;
            }
            case pico_lab::SurveyRequestResult::MethodNotAllowed:
                sendResponse(client, 405, "Method Not Allowed", "text/plain; charset=utf-8", String("Method not allowed\n"));
                break;
            case pico_lab::SurveyRequestResult::HostRejected:
                sendResponse(client, 421, "Misdirected Request", "text/plain; charset=utf-8", String("Host rejected\n"));
                break;
            case pico_lab::SurveyRequestResult::BadRequest:
                sendResponse(client, 400, "Bad Request", "text/plain; charset=utf-8", String("Bad request\n"));
                break;
        }
    }
    client.stop();
}

void printAccessInstructions() {
    Serial.print("Join ");
    Serial.println(AP_NAME);
    Serial.print("Password: ");
    Serial.println(ap_password);
    Serial.println("Open http://192.168.4.1/ (surveyed SSIDs are visible to every connected client).");
}

}  // namespace

void setup() {
    Serial.begin(115200);
    const uint32_t wait_started = millis();
    while (!Serial && millis() - wait_started < 1500U) {
        delay(10);
    }
    Serial.println("Scanning before access-point startup...");
    captureSurvey();

    WiFi.mode(WIFI_AP);
    const IPAddress address(192, 168, 4, 1);
    makeReadableSetupPassword();
    if (!WiFi.softAPConfig(address, address, IPAddress(255, 255, 255, 0)) || !WiFi.softAP(AP_NAME, ap_password)) {
        Serial.println("Access-point startup failed; reboot to retry.");
        return;
    }
    server.begin();
    server.setNoDelay(true);
    access_ready = true;
    printAccessInstructions();
    last_serial_reminder = millis();
}

void loop() {
    if (!access_ready) {
        delay(100);
        return;
    }
    handleClient();
    const uint32_t now = millis();
    if (now - last_serial_reminder >= 30000U) {
        last_serial_reminder = now;
        printAccessInstructions();
    }
    delay(1);
}
