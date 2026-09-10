#include <Arduino.h>
#include <Preferences.h>

#include "app_support.h"

namespace {
LocalPortal portal;
Preferences preferences;
uint64_t boot_count = 0;
bool persistence_ready = false;
bool restart_requested = false;
uint32_t restart_at_ms = 0;

void serveHome() {
    String mood;
    if (boot_count == 1) mood = "A fresh exhibit.";
    else if (boot_count < 10) mood = "Still practically new.";
    else if (boot_count < 100) mood = "Developing historical significance.";
    else mood = "Please stop rebooting the artifact.";
    char count[32];
    snprintf(count, sizeof(count), "%llu", static_cast<unsigned long long>(boot_count));
    String body = "<p class=muted>Join <code>ESP32-Reboot-Museum</code>. This count lives in NVS and survives power loss.</p><div class=card style='text-align:center'><small>LIFETIME BOOTS</small><h2 style='font-size:64px;margin:8px'>";
    body += count;
    body += "</h2><p>" + mood + "</p><small>Storage: " + String(persistence_ready ? "persistent" : "unavailable") + "</small></div><div class=grid><button id=reboot>Reboot once</button><button id=clear>Clear history</button></div>";
    const String script = "const act=async p=>{const r=await fetch(p,{method:'POST'});if(!r.ok){alert(await r.text());return}document.body.textContent='Rebooting the exhibit…'};document.querySelector('#reboot').onclick=()=>act('./reboot');document.querySelector('#clear').onclick=()=>act('./clear');";
    portal.server.send(200, "text/html; charset=utf-8", pageShell("Reboot Museum", body, script));
}

void scheduleRestart() {
    portal.server.send(202, "application/json", "{\"restarting\":true}");
    restart_requested = true;
    restart_at_ms = millis();
}

void clearHistory() {
    if (!persistence_ready || !preferences.clear()) {
        portal.server.send(503, "application/json", "{\"error\":\"NVS clear failed\"}");
        persistence_ready = false;
        return;
    }
    scheduleRestart();
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    if (preferences.begin("bootmuseum", false)) {
        const uint64_t stored_count = preferences.getULong64("boots", 0);
        const uint64_t next_count = stored_count + 1U;
        persistence_ready = preferences.putULong64("boots", next_count) == sizeof(uint64_t);
        boot_count = persistence_ready ? next_count : stored_count;
        if (!persistence_ready) Serial.println("Boot count NVS write failed");
    } else {
        Serial.println("Boot count NVS open failed");
    }
    portal.server.on("/", HTTP_GET, serveHome);
    portal.server.on("/reboot", HTTP_POST, scheduleRestart);
    portal.server.on("/clear", HTTP_POST, clearHistory);
    portal.begin("ESP32-Reboot-Museum");
}

void loop() {
    portal.handle();
    if (restart_requested && static_cast<uint32_t>(millis() - restart_at_ms) >= 500U) ESP.restart();
    delay(2);
}
