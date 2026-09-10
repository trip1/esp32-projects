#include <Arduino.h>

#include "lab_logic.h"
#include "rgb_support.h"

namespace {
constexpr uint32_t kUnitMs = 140;
const String message = "HELLO WORLD";

void pulse(uint32_t duration_ms) {
    setBoardRgb(24, 18, 0);
    delay(duration_ms);
    setBoardRgb(0, 0, 0);
    delay(kUnitMs);
}

void sendMessage() {
    const std::string encoded = encodeMorse(message.c_str());
    Serial.printf("%s -> %s\n", message.c_str(), encoded.c_str());
    for (const char symbol : encoded) {
        if (symbol == '.') pulse(kUnitMs);
        else if (symbol == '-') pulse(kUnitMs * 3U);
        else if (symbol == '/') delay(kUnitMs * 4U);
        else if (symbol == ' ') delay(kUnitMs * 2U);
    }
    delay(2500);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    setBoardRgb(0, 0, 0);
    Serial.println("Morse beacon ready");
}

void loop() {
    sendMessage();
}
