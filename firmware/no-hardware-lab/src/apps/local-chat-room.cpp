#include <Arduino.h>
#include <WebSocketsServer.h>

#include "app_support.h"
#include "lab_logic.h"

namespace {
LocalPortal portal;
WebSocketsServer socket_server(81);

void socketEvent(uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_CONNECTED) {
        socket_server.sendTXT(client, "SYSTEM: connected to Pocket Chat");
        return;
    }
    if (type != WStype_TEXT || length == 0) return;
    if (length > 160) {
        socket_server.sendTXT(client, "SYSTEM: message rejected (160-byte limit)");
        return;
    }
    const std::string message = boundedText(
        std::string(reinterpret_cast<const char*>(payload), length), 160);
    String broadcast = "guest-" + String(client + 1U) + ": ";
    broadcast += message.c_str();
    socket_server.broadcastTXT(broadcast);
}

void serveHome() {
    String body = "<p class=muted>Join <code>ESP32-Pocket-Chat</code>. Messages are live, temporary, and visible to everyone connected to this open network.</p>";
    body += "<div id=messages class=card style='height:280px;overflow:auto'></div><form id=chat class=grid><input id=message maxlength=160 autocomplete=off placeholder='Say something nearby' required><button class=primary>Send</button></form>";
    const String script = "const out=document.querySelector('#messages'),input=document.querySelector('#message'),ws=new WebSocket(`ws://${location.hostname}:81/`);ws.onmessage=e=>{const p=document.createElement('p');p.textContent=e.data;out.append(p);out.scrollTop=out.scrollHeight};document.querySelector('#chat').onsubmit=e=>{e.preventDefault();if(ws.readyState===1&&input.value.trim()){ws.send(input.value.slice(0,160));input.value=''}};";
    portal.server.send(200, "text/html; charset=utf-8", pageShell("Pocket Chat Room", body, script));
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    portal.server.on("/", HTTP_GET, serveHome);
    portal.begin("ESP32-Pocket-Chat");
    socket_server.begin();
    socket_server.onEvent(socketEvent);
}

void loop() {
    portal.handle();
    socket_server.loop();
    delay(2);
}
