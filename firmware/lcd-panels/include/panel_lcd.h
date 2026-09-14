#pragma once

class PanelLcd1602 {
public:
    bool begin();
    bool show(const char top[17], const char bottom[17]);

private:
    static bool transmit(unsigned char value, unsigned long started, unsigned long timeout_ms);
    static bool writeNibble(unsigned char nibble, bool data, unsigned long started, unsigned long timeout_ms);
    static bool writeByte(unsigned char value, bool data, unsigned long started, unsigned long timeout_ms);
    static bool command(unsigned char value, unsigned long started, unsigned long timeout_ms);
    static bool writeText(const char value[17], unsigned long started, unsigned long timeout_ms);
};
