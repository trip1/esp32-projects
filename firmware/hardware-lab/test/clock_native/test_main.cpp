#include "ntp_clock_display.h"

#include <cassert>
#include <cstring>

int main() {
    char top[17]{};
    char bottom[17]{};

    formatLcd1602Clock(2026, 9, 13, 7, 5, 9, top, bottom);
    assert(std::strlen(top) == 16U);
    assert(std::strlen(bottom) == 16U);
    assert(std::strcmp(top, "Date 2026-09-13 ") == 0);
    assert(std::strcmp(bottom, "Time 07:05:09   ") == 0);

    formatLcd1602Unavailable(top, bottom);
    assert(std::strcmp(top, "NTP Desk Clock  ") == 0);
    assert(std::strcmp(bottom, "Time unavailable") == 0);
    return 0;
}
