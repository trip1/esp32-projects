#include <Arduino.h>
#include <WiFi.h>

#include "app_support.h"

namespace {
LocalPortal portal;

String securityName(wifi_auth_mode_t mode) {
    return mode == WIFI_AUTH_OPEN ? "Open" : "Secured";
}

void serveSurvey() {
    const int count = WiFi.scanNetworks(false, true);
    String rows;
    if (count <= 0) {
        rows = "<tr><td colspan=4>No networks found. Try another scan.</td></tr>";
    } else {
        rows.reserve(static_cast<unsigned>(count) * 110U);
        for (int index = 0; index < count; ++index) {
            rows += "<tr><td>" + htmlEscape(WiFi.SSID(index)) + "</td><td>" + String(WiFi.RSSI(index));
            rows += " dBm</td><td>" + String(WiFi.channel(index)) + "</td><td>";
            rows += securityName(WiFi.encryptionType(index)) + "</td></tr>";
        }
    }
    WiFi.scanDelete();

    String body = "<p class=muted>Connect to <code>ESP32-WiFi-Surveyor</code>. This page scans from the board, not your browser.</p>";
    body += "<form method=get><button class=primary type=submit>Scan again</button></form>";
    body += "<div class=card><table><thead><tr><th>Network</th><th>Signal</th><th>Channel</th><th>Security</th></tr></thead><tbody>";
    body += rows + "</tbody></table></div><p class=muted>RSSI closer to 0 is stronger. Channel overlap and walls affect real performance.</p>";
    portal.server.send(200, "text/html; charset=utf-8", pageShell("Wi-Fi Surveyor", body));
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    portal.server.on("/", HTTP_GET, serveSurvey);
    portal.begin("ESP32-WiFi-Surveyor");
}

void loop() {
    portal.handle();
    delay(2);
}
