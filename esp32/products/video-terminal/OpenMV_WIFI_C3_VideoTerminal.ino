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
volatile bool videoClientActive = false;

String makeStatusJson() {
    const auto stats = videoReceiver.getStats();
    String json = "{";
    json += "\"video_active\":" + String(videoClientActive ? "true" : "false");
    json += ",\"rx_ok_frames\":" + String(stats.ok_frames);
    json += ",\"rx_bad_frames\":" + String(stats.bad_frames);
    json += ",\"rx_bad_header_count\":" + String(stats.bad_headers);
    json += ",\"rx_dropped_frames\":" + String(stats.dropped_frames);
    json += ",\"rx_timeout_count\":" + String(stats.timeouts);
    json += ",\"rx_oversize_count\":" + String(stats.oversize);
    json += ",\"rx_frame_id_gap_count\":" + String(stats.frame_id_gaps);
    json += "}";
    return json;
}

void handleRoot() {
    const char html[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>OpenMV Video Terminal</title></head><body>"
        "<h2>OpenMV Video Terminal</h2>"
        "<p>ESP32-C3 core video receiver</p>"
        "<img src='/stream' style='max-width:100%;height:auto'>"
        "<pre id='status'>loading...</pre>"
        "<script>setInterval(async()=>{try{status.textContent=JSON.stringify(await (await fetch('/api/status')).json(),null,2)}catch(e){}},1000)</script>"
        "</body></html>";
    server.send(200, "text/html; charset=utf-8", html);
}

void handleStatus() {
    server.send(200, "application/json", makeStatusJson());
}

void handleStream() {
    videoClientActive = true;
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "multipart/x-mixed-replace; boundary=frame", "");

    uint32_t lastFrameMs = millis();
    while (server.client().connected()) {
        SpiVideoReceiver::FrameView frame;
        if (videoReceiver.acquireLatestFrame(frame)) {
            String header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                            String(frame.size) + "\r\nCache-Control: no-cache\r\n\r\n";
            server.sendContent(header);
            server.sendContent(reinterpret_cast<const char*>(frame.data), frame.size);
            server.sendContent("\r\n");
            videoReceiver.releaseFrame(frame);
            lastFrameMs = millis();
        } else {
            delay(5);
        }

        if (millis() - lastFrameMs > 5000) {
            break;
        }
    }
    videoClientActive = false;
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[VIDEO TERMINAL] boot");

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
    Serial.print("[WIFI] AP IP: ");
    Serial.println(WiFi.softAPIP());

    if (MDNS.begin(MDNS_NAME)) {
        Serial.println("[mDNS] http://openmv-wifi.local/");
    }

    if (!videoReceiver.begin()) {
        Serial.println("[VIDEO RX] begin failed");
        while (true) {
            delay(1000);
        }
    }

    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/stream", HTTP_GET, handleStream);
    server.begin();

    Serial.println("[VIDEO RX] SPI2 slave ready: SCK=4 MISO=5 MOSI=6 CS=7");
    Serial.println("[HTTP] server ready");
}

void loop() {
    server.handleClient();
    // Arduino-ESP32 的 MDNSResponder 不提供 update()；mDNS 在后台任务中处理。
    delay(2);
}
}
