#include <Arduino.h>

namespace {

const char* boardName() {
#if defined(ARDUINO_RASPBERRY_PI_PICO_2W)
    return "Raspberry Pi Pico 2 W";
#elif defined(ARDUINO_RASPBERRY_PI_PICO_2)
    return "Raspberry Pi Pico 2";
#elif defined(ARDUINO_RASPBERRY_PI_PICO_W)
    return "Raspberry Pi Pico W";
#else
    return "Raspberry Pi Pico";
#endif
}

const char* chipName() {
#if defined(ARDUINO_RASPBERRY_PI_PICO_2W) || defined(ARDUINO_RASPBERRY_PI_PICO_2)
    return "RP2350";
#else
    return "RP2040";
#endif
}

bool led_on = false;
uint32_t last_toggle = 0;
uint32_t last_report = 0;

}  // namespace

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    Serial.begin(115200);
    const uint32_t wait_started = millis();
    while (!Serial && millis() - wait_started < 1500U) {
        delay(10);
    }
    Serial.println();
    Serial.println("DS9 Pico Board Check");
    Serial.print("Board: ");
    Serial.println(boardName());
    Serial.print("Chip: ");
    Serial.println(chipName());
    Serial.print("CPU: ");
    Serial.print(static_cast<unsigned long>(F_CPU / 1000000UL));
    Serial.println(" MHz");
    Serial.print("Flash: ");
    Serial.print(static_cast<unsigned long>(PICO_FLASH_SIZE_BYTES / (1024UL * 1024UL)));
    Serial.println(" MiB");
    Serial.println("Heartbeat active on the onboard status LED.");
}

void loop() {
    const uint32_t now = millis();
    if (now - last_toggle >= 500U) {
        last_toggle = now;
        led_on = !led_on;
        digitalWrite(LED_BUILTIN, led_on ? HIGH : LOW);
    }
    if (now - last_report >= 5000U) {
        last_report = now;
        Serial.print("uptime_ms=");
        Serial.println(now);
    }
    delay(1);
}
