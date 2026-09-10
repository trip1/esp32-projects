#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

class LocalPortal {
public:
    WebServer server{80};

    void begin(const char* ssid);
    void handle();
    String url() const;

private:
    DNSServer dns_;
};

String htmlEscape(const String& input);
String pageShell(const String& title, const String& body, const String& script = "");
