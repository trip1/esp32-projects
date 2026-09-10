#include "app_support.h"

void LocalPortal::begin(const char* ssid) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ssid, nullptr, 1, 0, 4);
    dns_.start(53, "*", WiFi.softAPIP());
    server.on("/generate_204", [this]() { server.sendHeader("Location", "/", true); server.send(302); });
    server.on("/hotspot-detect.html", [this]() { server.sendHeader("Location", "/", true); server.send(302); });
    server.onNotFound([this]() { server.sendHeader("Location", "/", true); server.send(302); });
    server.begin();
    Serial.printf("Open %s or %s\n", url().c_str(), ssid);
}

void LocalPortal::handle() {
    dns_.processNextRequest();
    server.handleClient();
}

String LocalPortal::url() const {
    return "http://" + WiFi.softAPIP().toString();
}

String htmlEscape(const String& input) {
    String output;
    output.reserve(input.length() + 8);
    for (const char character : input) {
        switch (character) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&#39;"; break;
            default: output += character; break;
        }
    }
    return output;
}

String pageShell(const String& title, const String& body, const String& script) {
    String page;
    page.reserve(body.length() + script.length() + 1800);
    page += F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>");
    page += F("<meta charset=utf-8><title>");
    page += htmlEscape(title);
    page += F("</title><style>body{margin:0;background:#0b1014;color:#eef5f0;font:16px/1.5 system-ui,sans-serif}main{max-width:760px;margin:auto;padding:32px 20px}h1{font-size:clamp(30px,8vw,54px);line-height:1;margin:12px 0 28px}h2{margin-top:32px}.tag{color:#a7f46a;font:12px monospace;letter-spacing:.12em}.card{background:#151e23;border:1px solid #2c393f;padding:20px;margin:14px 0}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}button,input{min-height:46px;border-radius:5px;border:1px solid #3b4a51;background:#1b272d;color:#eef5f0;padding:0 14px;font:inherit}button{cursor:pointer}button.primary{background:#a7f46a;color:#10200a;border:0;font-weight:700}table{width:100%;border-collapse:collapse;font-size:14px}th,td{text-align:left;padding:10px 7px;border-bottom:1px solid #2c393f}small,.muted{color:#9caaa5}code{font-family:ui-monospace,monospace;color:#a7f46a}a{color:#a7f46a}</style></head><body><main><p class=tag>");
    page += htmlEscape(ESP.getChipModel());
    page += F(" · LOCAL ONLY</p><h1>");
    page += htmlEscape(title);
    page += "</h1>";
    page += body;
    if (!script.isEmpty()) {
        page += "<script>";
        page += script;
        page += "</script>";
    }
    page += F("</main></body></html>");
    return page;
}
