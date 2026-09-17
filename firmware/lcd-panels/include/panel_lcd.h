#pragma once

class PanelLcd1602 {
public:
    bool begin();
    bool show(const char top[17], const char bottom[17]);
    unsigned char detectedAddress() const { return address_; }

private:
    bool transmit(unsigned char value, unsigned long started, unsigned long timeout_ms);
    bool writeNibble(unsigned char nibble, bool data, unsigned long started, unsigned long timeout_ms);
    bool writeByte(unsigned char value, bool data, unsigned long started, unsigned long timeout_ms);
    bool command(unsigned char value, unsigned long started, unsigned long timeout_ms);
    bool writeText(const char value[17], unsigned long started, unsigned long timeout_ms);
    unsigned char address_ = PANEL_LCD_ADDRESS;
    bool has_frame_ = false;
    char previous_top_[17]{};
    char previous_bottom_[17]{};
};
