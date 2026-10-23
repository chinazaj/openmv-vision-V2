#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include "../../core/video/spi_video_receiver.h"
#include "../../core/video/spi_video_receiver.cpp"

namespace {
constexpr char AP_SSID[] = "openmv-wifi";
constexpr char MDNS_NAME[] = "openmv-wifi";

WebServer server(80);
SpiVideoReceiver videoReceiver;

String makeStatusJson() {
    const auto stats = videoReceiver.getStats();
    String json = "{";
    json += "\"wifi_clients\":" + String(WiFi.softAPgetStationNum());
    json += ",\"rx_ok_frames\":" + String(stats.ok_frames);
    json += ",\"rx_bad_frames\":" + String(stats.bad_frames);
    json += ",\"rx_bad_header_count\":" + String(stats.bad_headers);
    json += ",\"rx_dropped_frames\":" + String(stats.dropped_frames);
    json += ",\"rx_timeout_count\":" + String(stats.timeouts);
    json += ",\"rx_oversize_count\":" + String(stats.oversize);
    json += ",\"rx_frame_id_gap_count\":" + String(stats.frame_id_gaps);
    return json + "}";
}

void handleRoot() {
    Serial.println("[HTTP] GET /");
    const char html[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>OpenMV Video Terminal</title></head><body>"
        "<h2>OpenMV Video Terminal</h2>"
        "<p>ESP32-C3 core video receiver</p>"
        "<p id='videoState'>Waiting for video frame...</p>"
        "<img id='video' style='max-width:100%;height:auto'>"
        "<pre id='status'>loading...</pre>"
        "<script>"
        "const img=document.getElementById('video');"
        "const state=document.getElementById('videoState');"
        "function nextFrame(){img.src='/frame.jpg?t='+Date.now()}"
        "img.onload=()=>{state.textContent='Frame received';setTimeout(nextFrame,100)};"
        "img.onerror=()=>{state.textContent='Waiting for frame from OpenMV...';setTimeout(nextFrame,500)};"
        "nextFrame();"
        "setInterval(async()=>{try{document.getElementById('status').textContent=JSON.stringify(await (await fetch('/api/status',{cache:'no-store'})).json(),null,2)}catch(e){}},1000);"
        "</script></body></html>";
    server.send(200, "text/html; charset=utf-8", html);
}

void handleHealth() {
    Serial.println("[HTTP] GET /health");
    server.send(200, "text/plain; charset=utf-8", "ok");
}

void handleStatus() {
    Serial.println("[HTTP] GET /api/status");
    server.send(200, "application/json", makeStatusJson());
}

void handleFrame() {
    SpiVideoReceiver::FrameView frame;
    if (!videoReceiver.acquireLatestFrame(frame)) {
        server.send(503, "text/plain; charset=utf-8", "No video frame received yet");
        return;
    }

    server.setContentLength(frame.size);
    server.send(200, "image/jpeg", "");
    server.sendContent(reinterpret_cast<const char*>(frame.data), frame.size);
    videoReceiver.releaseFrame(frame);
}
} // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[VIDEO TERMINAL] boot");

    WiFi.mode(WIFI_AP);
    const bool apStarted = WiFi.softAP(AP_SSID);
    Serial.printf("[WIFI] softAP start: %s\n", apStarted ? "OK" : "FAILED");
    Serial.print("[WIFI] AP IP: ");
    Serial.println(WiFi.softAPIP());

    if (MDNS.begin(MDNS_NAME)) {
        Serial.println("[mDNS] http://openmv-wifi.local/");
    } else {
        Serial.println("[mDNS] start failed; use http://192.168.4.1/");
    }

    if (!videoReceiver.begin()) {
        Serial.println("[VIDEO RX] begin failed");
        while (true) {
            delay(1000);
        }
    }

    server.on("/", HTTP_GET, handleRoot);
    server.on("/health", HTTP_GET, handleHealth);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/frame.jpg", HTTP_GET, handleFrame);
    server.begin();

    Serial.println("[VIDEO RX] SPI2 slave ready: SCK=4 MISO=5 MOSI=6 CS=7");
    Serial.println("[HTTP] server ready on port 80");
    Serial.println("[HTTP] test: http://192.168.4.1/health");
}

void loop() {
    server.handleClient();
    delay(2);
}
