#include <Arduino.h>

#include "app_support.h"

namespace {
LocalPortal portal;

uint32_t runIntegerWorkload() {
    volatile uint32_t value = 0x12345678U;
    const uint32_t started = micros();
    for (uint32_t index = 0; index < 500000U; ++index) {
        value ^= value << 13U;
        value ^= value >> 17U;
        value ^= value << 5U;
        value += index;
    }
    const uint32_t elapsed = static_cast<uint32_t>(micros() - started);
    Serial.printf("Benchmark checksum: %lu\n", static_cast<unsigned long>(value));
    return elapsed;
}

uint32_t runMemoryWorkload() {
    static volatile uint8_t buffer[8192];
    uint32_t checksum = 0;
    const uint32_t started = micros();
    for (uint32_t pass = 0; pass < 100U; ++pass) {
        for (size_t index = 0; index < sizeof(buffer); ++index) buffer[index] = static_cast<uint8_t>(index + pass);
        for (const uint8_t value : buffer) checksum += value;
    }
    const uint32_t elapsed = static_cast<uint32_t>(micros() - started);
    Serial.printf("Memory checksum: %lu\n", static_cast<unsigned long>(checksum));
    return elapsed;
}

void serveResults() {
    const uint32_t integer_us = runIntegerWorkload();
    const uint32_t memory_us = runMemoryWorkload();
    char response[180];
    snprintf(response, sizeof(response), "{\"integer_microseconds\":%lu,\"memory_microseconds\":%lu,\"cpu_mhz\":%u,\"free_heap\":%u}",
             static_cast<unsigned long>(integer_us), static_cast<unsigned long>(memory_us), ESP.getCpuFreqMHz(), ESP.getFreeHeap());
    portal.server.send(200, "application/json", response);
}

void serveHome() {
    String body = "<p class=muted>Join <code>ESP32-Tiny-Benchmark</code>. Each run performs the same bounded workloads so firmware builds and clock settings can be compared.</p>";
    body += "<button class=primary id=run>Run benchmark</button><div id=result class='card grid'><p class=muted>No result yet.</p></div>";
    const String script = "const box=document.querySelector('#result');document.querySelector('#run').onclick=async e=>{e.target.disabled=true;box.textContent='Benchmarking…';const d=await fetch('./api/run',{method:'POST'}).then(r=>r.json());box.replaceChildren(...Object.entries(d).map(([k,v])=>{const p=document.createElement('p');p.textContent=`${k.replaceAll('_',' ')}: ${v}`;return p}));e.target.disabled=false};";
    portal.server.send(200, "text/html; charset=utf-8", pageShell("Tiny Benchmark Lab", body, script));
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    portal.server.on("/", HTTP_GET, serveHome);
    portal.server.on("/api/run", HTTP_POST, serveResults);
    portal.begin("ESP32-Tiny-Benchmark");
}

void loop() {
    portal.handle();
    delay(2);
}
