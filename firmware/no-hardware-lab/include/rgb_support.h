#pragma once

#include <Arduino.h>

#ifndef RGB_BUILTIN
#define RGB_BUILTIN 8
#endif

inline void setBoardRgb(uint8_t red, uint8_t green, uint8_t blue) {
    rgbLedWrite(RGB_BUILTIN, red, green, blue);
}
