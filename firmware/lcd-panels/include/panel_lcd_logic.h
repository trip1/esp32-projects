#pragma once

struct PanelLcdUpdatePlan {
    bool top = false;
    bool bottom = false;
};

PanelLcdUpdatePlan panelLcdUpdatePlan(bool has_previous,
                                      const char previous_top[17], const char previous_bottom[17],
                                      const char next_top[17], const char next_bottom[17]);
