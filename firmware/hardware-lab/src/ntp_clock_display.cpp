#include "ntp_clock_display.h"

#include <cstdio>
#include <cstring>

namespace {
void fillLine(const char* value, char output[17]) {
    const std::size_t length = std::strlen(value);
    for (std::size_t index = 0; index < 16U; ++index) output[index] = index < length ? value[index] : ' ';
    output[16] = '\0';
}
}  // namespace

void formatLcd1602Clock(int year, int month, int day, int hour, int minute, int second, char top[17], char bottom[17]) {
    char date[17]{};
    char time[17]{};
    std::snprintf(date, sizeof(date), "Date %04d-%02d-%02d", year, month, day);
    std::snprintf(time, sizeof(time), "Time %02d:%02d:%02d", hour, minute, second);
    fillLine(date, top);
    fillLine(time, bottom);
}

void formatLcd1602Unavailable(char top[17], char bottom[17]) {
    fillLine("NTP Desk Clock", top);
    fillLine("Time unavailable", bottom);
}
