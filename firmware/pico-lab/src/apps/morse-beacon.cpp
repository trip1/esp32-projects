#include <Arduino.h>

#include "pico_logic.h"

namespace {

constexpr char MESSAGE[] = "HELLO WORLD";
constexpr uint32_t DOT_MS = 140;

void setLed(bool on) {
    digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

void mark(uint32_t duration) {
    setLed(true);
    delay(duration);
    setLed(false);
    delay(DOT_MS);
}

void character(char value) {
    const std::string_view code = pico_lab::morseFor(value);
    if (code.empty()) {
        delay(DOT_MS * 7U);
        return;
    }
    Serial.print(value);
    Serial.print(' ');
    Serial.println(code.data());
    for (const char symbol : code) {
        mark(symbol == '.' ? DOT_MS : DOT_MS * 3U);
    }
}

}  // namespace

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    setLed(false);
    Serial.begin(115200);
    const uint32_t wait_started = millis();
    while (!Serial && millis() - wait_started < 1500U) {
        delay(10);
    }
    Serial.println("DS9 Pico Morse Beacon: HELLO WORLD");
}

void loop() {
    constexpr std::size_t message_length = sizeof(MESSAGE) - 1;
    for (std::size_t index = 0; index < message_length; ++index) {
        const char value = MESSAGE[index];
        if (value == ' ') {
            continue;
        }
        character(value);
        if (index + 1 < message_length) {
            delay(DOT_MS * pico_lab::additionalMorseGapUnits(MESSAGE[index + 1]));
        }
    }
    Serial.println("-- repeat --");
    delay(DOT_MS * 6U);
}
