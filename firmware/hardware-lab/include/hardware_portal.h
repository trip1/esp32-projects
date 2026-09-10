#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

class HardwarePortal {
public:
    WebServer server{80};
    bool begin(const char* ssid);
    void handle();
    String url() const;
private:
    DNSServer dns_;
};

String hardwarePage(const String& title, const String& body, const String& script = "");
