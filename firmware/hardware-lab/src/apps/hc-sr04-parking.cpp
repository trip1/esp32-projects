#include <Arduino.h>
#include <cmath>

#include "hardware_logic.h"
#include "hardware_portal.h"

#ifndef TRIGGER_PIN
#error "TRIGGER_PIN is required"
#endif
#ifndef ECHO_PIN
#error "ECHO_PIN is required"
#endif

namespace {
HardwarePortal portal;
float samples[5]{NAN, NAN, NAN, NAN, NAN};
size_t sample_index = 0U;
uint32_t last_sample_ms = 0U;

float currentDistance() {
    return medianDistance(samples, 5U);
}

void sendStatus() {
    const float distance = currentDistance();
    char body[128];
    if (std::isfinite(distance)) {
        std::snprintf(body, sizeof(body), "{\"valid\":true,\"distance_cm\":%.1f,\"echo_pin\":%d,\"trigger_pin\":%d}",
                      distance, ECHO_PIN, TRIGGER_PIN);
    } else {
        std::snprintf(body, sizeof(body), "{\"valid\":false,\"echo_pin\":%d,\"trigger_pin\":%d}", ECHO_PIN, TRIGGER_PIN);
    }
    portal.server.send(200, "application/json", body);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(TRIGGER_PIN, OUTPUT);
    digitalWrite(TRIGGER_PIN, LOW);
    pinMode(ECHO_PIN, INPUT);
    portal.server.on("/", []() {
        const String body = F("<div class=card><div class=value><span id=distance>--</span> cm</div><p id=state>Waiting for echo</p></div>"
                              "<div class=card><label>Full-scale distance (cm) <input id=max type=number min=10 max=400 value=200></label>"
                              "<p>Fill/parking estimate: <strong id=percent>--</strong></p></div>"
                              "<p><small>Use a resistor divider or level shifter on HC-SR04 Echo; never feed a 5 V echo directly into an ESP32 GPIO.</small></p>");
        const String script = F("const d=document.querySelector('#distance'),s=document.querySelector('#state'),p=document.querySelector('#percent'),m=document.querySelector('#max');"
                                "async function tick(){try{const r=await fetch('/status',{cache:'no-store'}),j=await r.json();if(j.valid){d.textContent=j.distance_cm.toFixed(1);s.textContent=j.distance_cm<30?'STOP / VERY CLOSE':j.distance_cm<80?'CAUTION':'CLEAR';const x=Math.max(0,Math.min(100,(1-j.distance_cm/Number(m.value||200))*100));p.textContent=x.toFixed(0)+'%';}else{s.textContent='No echo in range';d.textContent='--';p.textContent='--';}}catch(e){s.textContent='Disconnected';}setTimeout(tick,500)}tick();");
        portal.server.send(200, "text/html", hardwarePage("Ultrasonic Parking Assistant", body, script));
    });
    portal.server.on("/status", sendStatus);
    if (!portal.begin("ESP32-Parking-Assistant")) { delay(30000); ESP.restart(); }
}

void loop() {
    portal.handle();
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - last_sample_ms) >= 60U) {
        last_sample_ms = now;
        digitalWrite(TRIGGER_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(TRIGGER_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIGGER_PIN, LOW);
        samples[sample_index] = distanceCentimetersFromEcho(pulseIn(ECHO_PIN, HIGH, 30000UL));
        sample_index = (sample_index + 1U) % 5U;
    }
    delay(1);
}
