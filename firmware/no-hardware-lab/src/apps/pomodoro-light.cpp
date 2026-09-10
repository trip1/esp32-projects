#include <Arduino.h>

#include "lab_logic.h"
#include "rgb_support.h"

namespace {
uint32_t started_ms = 0;
uint32_t last_reported_seconds = UINT32_MAX;
bool last_working = true;
PomodoroTimer timer;

void showPhase(const PomodoroPhase& phase) {
    if (phase.working) {
        setBoardRgb(0, 24, 4);
    } else {
        setBoardRgb(0, 5, 28);
    }
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    started_ms = millis();
    timer.reset(started_ms);
    const auto phase = timer.advance(started_ms);
    showPhase(phase);
    Serial.println("Pomodoro started: 25 minutes focus, 5 minutes break");
}

void loop() {
    const auto phase = timer.advance(millis());
    if (phase.working != last_working) {
        last_working = phase.working;
        setBoardRgb(36, 20, 0);
        delay(250);
        showPhase(phase);
        Serial.println(phase.working ? "Focus session started" : "Break started");
    }
    if (phase.remaining_seconds != last_reported_seconds && phase.remaining_seconds % 30U == 0) {
        last_reported_seconds = phase.remaining_seconds;
        Serial.printf("%s: %lu:%02lu remaining\n", phase.working ? "Focus" : "Break",
                      static_cast<unsigned long>(phase.remaining_seconds / 60U),
                      static_cast<unsigned long>(phase.remaining_seconds % 60U));
    }
    delay(100);
}
