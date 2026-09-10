#include <Arduino.h>
#include <esp_system.h>

#include "app_support.h"

namespace {
LocalPortal portal;
bool restart_requested = false;
uint32_t restart_at_ms = 0;

void serveStatus() {
    char response[512];
    const uint64_t chip_id = ESP.getEfuseMac();
    snprintf(
        response,
        sizeof(response),
        "{\"chip\":\"%s\",\"revision\":%u,\"cores\":%u,\"cpu_mhz\":%u,\"uptime_seconds\":%lu,\"free_heap\":%u,\"minimum_free_heap\":%u,\"flash_bytes\":%u,\"reset_reason\":%d,\"chip_id\":\"%04x%08x\",\"wifi_mode\":%d,\"ap_clients\":%u}",
        ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz(),
        static_cast<unsigned long>(millis() / 1000U), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
        ESP.getFlashChipSize(), static_cast<int>(esp_reset_reason()),
        static_cast<unsigned>(chip_id >> 32U), static_cast<unsigned>(chip_id),
        static_cast<int>(WiFi.getMode()), WiFi.softAPgetStationNum());
    portal.server.send(200, "application/json", response);
}

void requestRestart() {
    portal.server.send(202, "application/json", "{\"restarting\":true}");
    restart_requested = true;
    restart_at_ms = millis();
}

void serveHome() {
    String body = "<p class=muted>Connect to <code>ESP32-Device-Console</code>. Values refresh every two seconds and never leave the board.</p>";
    body += "<div id=status class='card grid'>Loading diagnostics…</div><button id=restart>Restart board</button>";
    const String script = "const box=document.querySelector('#status');async function load(){const d=await fetch('./api/status').then(r=>r.json());const nodes=Object.entries(d).map(([k,v])=>{const wrap=document.createElement('div'),label=document.createElement('small'),value=document.createElement('code'),br=document.createElement('br');label.textContent=k.replaceAll('_',' ');value.textContent=String(v);wrap.append(label,br,value);return wrap});box.replaceChildren(...nodes)}load();setInterval(load,2000);document.querySelector('#restart').onclick=()=>fetch('./api/restart',{method:'POST'}).then(()=>box.textContent='Restarting…');";
    portal.server.send(200, "text/html; charset=utf-8", pageShell("Device Console", body, script));
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    portal.server.on("/", HTTP_GET, serveHome);
    portal.server.on("/api/status", HTTP_GET, serveStatus);
    portal.server.on("/api/restart", HTTP_POST, requestRestart);
    portal.begin("ESP32-Device-Console");
}

void loop() {
    portal.handle();
    if (restart_requested && static_cast<uint32_t>(millis() - restart_at_ms) >= 500U) ESP.restart();
    delay(2);
}
