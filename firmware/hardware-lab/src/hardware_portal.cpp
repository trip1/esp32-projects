#include "hardware_portal.h"

namespace {
String escape(const String& input) {
    String output;
    output.reserve(input.length() + 8U);
    for (const char character : input) {
        switch (character) {
            case '&': output += F("&amp;"); break;
            case '<': output += F("&lt;"); break;
            case '>': output += F("&gt;"); break;
            case '"': output += F("&quot;"); break;
            case '\'': output += F("&#39;"); break;
            default: output += character; break;
        }
    }
    return output;
}
}  // namespace

bool HardwarePortal::begin(const char* ssid) {
    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(ssid, nullptr, 1, false, 4)) return false;
    if (!dns_.start(53, "*", WiFi.softAPIP())) {
        WiFi.softAPdisconnect(true);
        return false;
    }
    server.on("/generate_204", [this]() { server.sendHeader("Location", "/", true); server.send(302); });
    server.on("/hotspot-detect.html", [this]() { server.sendHeader("Location", "/", true); server.send(302); });
    server.onNotFound([this]() { server.sendHeader("Location", "/", true); server.send(302); });
    server.begin();
    Serial.printf("Open %s on network %s\n", url().c_str(), ssid);
    return true;
}

void HardwarePortal::handle() {
    dns_.processNextRequest();
    server.handleClient();
}

String HardwarePortal::url() const {
    return "http://" + WiFi.softAPIP().toString();
}

String hardwarePage(const String& title, const String& body, const String& script) {
    String page;
    page.reserve(body.length() + script.length() + 1800U);
    page += F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><style>"
              "body{margin:0;background:#0b1014;color:#eef5f0;font:16px/1.5 system-ui}main{max-width:760px;margin:auto;padding:30px 20px}"
              "h1{font-size:clamp(2rem,8vw,3.5rem);line-height:1}.tag{color:#a7f46a;font:12px monospace;letter-spacing:.12em}"
              ".card{background:#151e23;border:1px solid #2c393f;padding:20px;margin:14px 0}.value{font-size:2.5rem;font-weight:750}"
              "button,input{min-height:44px;border:1px solid #3b4a51;border-radius:5px;background:#1b272d;color:#eef5f0;padding:0 12px;font:inherit}"
              "button{background:#a7f46a;color:#10200a;font-weight:700}small{color:#9caaa5}</style><main><p class=tag>");
    page += escape(ESP.getChipModel());
    page += F(" · LOCAL HARDWARE LAB</p><h1>");
    page += escape(title);
    page += F("</h1>");
    page += body;
    if (!script.isEmpty()) {
        page += F("<script>");
        page += script;
        page += F("</script>");
    }
    page += F("</main>");
    return page;
}
