#pragma once

#include <Arduino.h>

#ifndef RGB_BUILTIN
#error "This firmware target requires a board variant with an addressable RGB LED"
#endif

inline void setBoardRgb(uint8_t red, uint8_t green, uint8_t blue) {
    rgbLedWrite(RGB_BUILTIN, red, green, blue);
}
