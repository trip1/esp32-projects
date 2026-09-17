#include "panel_lcd_logic.h"

#include <cstring>

PanelLcdUpdatePlan panelLcdUpdatePlan(bool has_previous,
                                      const char previous_top[17], const char previous_bottom[17],
                                      const char next_top[17], const char next_bottom[17]) {
    if (!has_previous) return {true, true};
    return {
        std::memcmp(previous_top, next_top, 16U) != 0,
        std::memcmp(previous_bottom, next_bottom, 16U) != 0,
    };
}
