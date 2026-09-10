#include <Arduino.h>

#include "app_support.h"
#include "rgb_support.h"

namespace {
LocalPortal portal;
uint8_t red = 12;
uint8_t green = 28;
uint8_t blue = 8;

uint8_t channelValue(const String& name, uint8_t fallback) {
    if (!portal.server.hasArg(name)) return fallback;
    return static_cast<uint8_t>(constrain(portal.server.arg(name).toInt(), 0, 255));
}

void applyColor() {
    red = channelValue("r", red);
    green = channelValue("g", green);
    blue = channelValue("b", blue);
    setBoardRgb(red, green, blue);
    char response[80];
    snprintf(response, sizeof(response), "{\"r\":%u,\"g\":%u,\"b\":%u}", red, green, blue);
    portal.server.send(200, "application/json", response);
}

void serveHome() {
    String body = "<p class=muted>Connect to <code>ESP32-Pocket-Lamp</code>, then use the onboard RGB LED as a very tiny lamp.</p>";
    body += "<div class=card><label for=color>Color</label><br><input id=color type=color value=#0c1c08 style='width:100%;margin:12px 0'>";
    body += "<div class=grid><button data-c='255,30,8'>Warm</button><button data-c='0,30,255'>Blue</button><button data-c='0,255,40'>Green</button><button data-c='0,0,0'>Off</button></div></div>";
    const String script = "const send=(r,g,b)=>fetch(`./set?r=${r}&g=${g}&b=${b}`);document.querySelector('#color').oninput=e=>{const h=e.target.value;send(parseInt(h.slice(1,3),16),parseInt(h.slice(3,5),16),parseInt(h.slice(5),16))};document.querySelectorAll('[data-c]').forEach(b=>b.onclick=()=>send(...b.dataset.c.split(',')));";
    portal.server.send(200, "text/html; charset=utf-8", pageShell("Pocket RGB Lamp", body, script));
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    setBoardRgb(red, green, blue);
    portal.server.on("/", HTTP_GET, serveHome);
    portal.server.on("/set", HTTP_GET, applyColor);
    portal.begin("ESP32-Pocket-Lamp");
}

void loop() {
    portal.handle();
    delay(2);
}
