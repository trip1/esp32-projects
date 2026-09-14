#include "panel_lcd.h"

#include <Arduino.h>
#include <Wire.h>

#ifndef PANEL_SDA_PIN
#error "PANEL_SDA_PIN is required"
#endif
#ifndef PANEL_SCL_PIN
#error "PANEL_SCL_PIN is required"
#endif
#ifndef PANEL_LCD_ADDRESS
#error "PANEL_LCD_ADDRESS is required"
#endif

namespace {
constexpr unsigned char kEnable = 0x04U;
constexpr unsigned char kRegisterSelect = 0x01U;
constexpr unsigned char kBacklight = 0x08U;
}

bool PanelLcd1602::begin() {
    if (!Wire.begin(PANEL_SDA_PIN, PANEL_SCL_PIN)) return false;
    Wire.setTimeOut(25U);
    const unsigned long started = millis();
    delay(50);
    if (!writeNibble(0x30U, false, started, 250U)) return false;
    delay(5);
    if (!writeNibble(0x30U, false, started, 250U)) return false;
    delayMicroseconds(150);
    if (!writeNibble(0x30U, false, started, 250U)) return false;
    if (!writeNibble(0x20U, false, started, 250U)) return false;
    if (!command(0x28U, started, 250U) || !command(0x08U, started, 250U) || !command(0x01U, started, 250U)) return false;
    delay(2);
    return command(0x06U, started, 250U) && command(0x0CU, started, 250U);
}

bool PanelLcd1602::show(const char top[17], const char bottom[17]) {
    const unsigned long started = millis();
    return command(0x80U, started, 150U) && writeText(top, started, 150U)
        && command(0xC0U, started, 150U) && writeText(bottom, started, 150U);
}

bool PanelLcd1602::transmit(unsigned char value, unsigned long started, unsigned long timeout_ms) {
    const unsigned long elapsed = millis() - started;
    if (elapsed >= timeout_ms) return false;
    const unsigned long remaining = timeout_ms - elapsed;
    Wire.setTimeOut(remaining < 25U ? remaining : 25U);
    Wire.beginTransmission(PANEL_LCD_ADDRESS);
    if (Wire.write(value) != 1U) return false;
    return Wire.endTransmission(true) == 0U;
}

bool PanelLcd1602::writeNibble(unsigned char nibble, bool data, unsigned long started, unsigned long timeout_ms) {
    const unsigned char value = static_cast<unsigned char>((nibble & 0xF0U) | kBacklight | (data ? kRegisterSelect : 0U));
    return transmit(value, started, timeout_ms)
        && transmit(static_cast<unsigned char>(value | kEnable), started, timeout_ms)
        && transmit(value, started, timeout_ms);
}

bool PanelLcd1602::writeByte(unsigned char value, bool data, unsigned long started, unsigned long timeout_ms) {
    return writeNibble(value, data, started, timeout_ms)
        && writeNibble(static_cast<unsigned char>(value << 4U), data, started, timeout_ms);
}

bool PanelLcd1602::command(unsigned char value, unsigned long started, unsigned long timeout_ms) {
    return writeByte(value, false, started, timeout_ms);
}

bool PanelLcd1602::writeText(const char value[17], unsigned long started, unsigned long timeout_ms) {
    for (unsigned char index = 0U; index < 16U; ++index) {
        if (!writeByte(static_cast<unsigned char>(value[index]), true, started, timeout_ms)) return false;
    }
    return true;
}
