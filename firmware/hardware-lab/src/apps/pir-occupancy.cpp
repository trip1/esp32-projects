#include <Arduino.h>
#include <atomic>

#include "hardware_logic.h"
#include "hardware_portal.h"

#ifndef PIR_PIN
#error "PIR_PIN is required"
#endif

namespace {
constexpr uint32_t kHoldMs = 30000U;
HardwarePortal portal;
volatile uint32_t pending_motion_edges = 0U;
bool motion_seen = false;
uint32_t last_motion_ms = 0U;
uint32_t event_count = 0U;

void IRAM_ATTR onMotion() {
    if (pending_motion_edges != 0xffffffffU) pending_motion_edges = pending_motion_edges + 1U;
}

void sendStatus() {
    const uint32_t now = millis();
    const bool occupied = occupancyActive(now, last_motion_ms, kHoldMs, motion_seen);
    const uint32_t remaining = occupied ? (kHoldMs - static_cast<uint32_t>(now - last_motion_ms)) / 1000U : 0U;
    char body[144];
    std::snprintf(body, sizeof(body),
                  "{\"occupied\":%s,\"event_count\":%lu,\"seconds_remaining\":%lu,\"pir_pin\":%d}",
                  occupied ? "true" : "false", static_cast<unsigned long>(event_count),
                  static_cast<unsigned long>(remaining), PIR_PIN);
    portal.server.send(200, "application/json", body);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(PIR_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIR_PIN), onMotion, RISING);
    portal.server.on("/", []() {
        const String body = F("<div class=card><div class=value id=state>Waiting</div><p>Motion events: <strong id=count>0</strong></p>"
                              "<p>Occupied hold timer: <strong id=remaining>0</strong> seconds</p></div>"
                              "<p><small>The ISR records only a saturating edge count. State updates and the rollover-safe 30-second hold timer run in the main loop.</small></p>");
        const String script = F("const s=document.querySelector('#state'),c=document.querySelector('#count'),r=document.querySelector('#remaining');"
                                "async function tick(){try{const x=await fetch('/status',{cache:'no-store'}),j=await x.json();s.textContent=j.occupied?'OCCUPIED':'CLEAR';c.textContent=j.event_count;r.textContent=j.seconds_remaining;}catch(e){s.textContent='DISCONNECTED';}setTimeout(tick,500)}tick();");
        portal.server.send(200, "text/html", hardwarePage("PIR Occupancy Timer", body, script));
    });
    portal.server.on("/status", sendStatus);
    if (!portal.begin("ESP32-PIR-Occupancy")) { delay(30000); ESP.restart(); }
}

void loop() {
    if (pending_motion_edges != 0U) {
        noInterrupts();
        const uint32_t detected = pending_motion_edges;
        pending_motion_edges = 0U;
        interrupts();
        if (detected != 0U) {
            motion_seen = true;
            last_motion_ms = millis();
            event_count = saturatingEventAdd(event_count, detected);
            Serial.printf("Motion events %lu (+%lu)\n", static_cast<unsigned long>(event_count), static_cast<unsigned long>(detected));
        }
    }
    portal.handle();
    delay(1);
}
