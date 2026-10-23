#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

#include "../../core/video/spi_video_receiver.h"
#include "../../core/video/spi_video_receiver.cpp"

namespace {
constexpr char AP_SSID[] = "openmv-wifi";
constexpr char MDNS_NAME[] = "openmv-wifi";
constexpr uint8_t DNS_PORT = 53;

const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_GATEWAY(192, 168, 4, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

WebServer server(80);
DNSServer dnsServer;
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

void redirectToPortal() {
    const String uri = server.uri();
    Serial.printf("[HTTP] captive redirect: %s -> http://192.168.4.1/\n", uri.c_str());
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain; charset=utf-8", "Redirecting to OpenMV Video Terminal");
}

void handleRoot() {
    Serial.println("[HTTP] GET /");
    const char html[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta http-equiv='Cache-Control' content='no-cache'>"
        "<title>OpenMV Video Terminal</title></head><body>"
        "<h2>OpenMV Video Terminal</h2>"
        "<p>ESP32-C3 core video receiver</p>"
        "<p id='videoState'>Waiting for video frame...</p>"
        "<img id='video' style='max-width:100%;height:auto;background:#111'>"
        "<pre id='status'>loading...</pre>"
        "<script>"
        "const img=document.getElementById('video');"
        "const state=document.getElementById('videoState');"
        "let lastObjectUrl=null;"
        "async function nextFrame(){"
        " try{"
        "  const response=await fetch('/frame.jpg?t='+Date.now(),{cache:'no-store'});"
        "  if(!response.ok)throw new Error('frame unavailable: '+response.status);"
        "  const blob=await response.blob();"
        "  if(!blob.size||blob.type.indexOf('image/')!==0)throw new Error('invalid JPEG response');"
        "  const url=URL.createObjectURL(blob);"
        "  const probe=new Image();"
        "  probe.onload=()=>{img.src=url;if(lastObjectUrl)URL.revokeObjectURL(lastObjectUrl);lastObjectUrl=url;state.textContent='Frame received';setTimeout(nextFrame,80)};"
        "  probe.onerror=()=>{URL.revokeObjectURL(url);state.textContent='Waiting for valid JPEG frame...';setTimeout(nextFrame,250)};"
        "  probe.src=url;"
        " }catch(e){state.textContent='Waiting for next frame; keeping last image';setTimeout(nextFrame,250)}"
        "}"
        "nextFrame();"
        "setInterval(async()=>{try{document.getElementById('status').textContent=JSON.stringify(await (await fetch('/api/status',{cache:'no-store'})).json(),null,2)}catch(e){}},1000);"
        "</script></body></html>";
    server.sendHeader("Cache-Control", "no-store");
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
    static uint32_t lastNoFrameLogMs = 0;
    static uint32_t lastFrameLogMs = 0;
    SpiVideoReceiver::FrameView frame;
    if (!videoReceiver.acquireLatestFrame(frame)) {
        const uint32_t now = millis();
        if (now - lastNoFrameLogMs >= 5000) {
            const auto stats = videoReceiver.getStats();
            Serial.printf("[VIDEO HTTP] no frame available; rx_ok=%lu bad=%lu headers=%lu dropped=%lu timeout=%lu\n",
                          static_cast<unsigned long>(stats.ok_frames),
                          static_cast<unsigned long>(stats.bad_frames),
                          static_cast<unsigned long>(stats.bad_headers),
                          static_cast<unsigned long>(stats.dropped_frames),
                          static_cast<unsigned long>(stats.timeouts));
            lastNoFrameLogMs = now;
        }
        server.send(503, "text/plain; charset=utf-8", "No video frame received yet");
        return;
    }

    server.setContentLength(frame.size);
    server.send(200, "image/jpeg", "");
    const size_t written = server.client().write(frame.data, frame.size);
    if (written != frame.size) {
        Serial.printf("[VIDEO HTTP] short JPEG write: %u/%u bytes\n",
                      static_cast<unsigned>(written), static_cast<unsigned>(frame.size));
    }
    const uint32_t now = millis();
    if (now - lastFrameLogMs >= 5000) {
        Serial.printf("[VIDEO HTTP] JPEG served: frame_id=%u size=%u bytes\n",
                      frame.frame_id, static_cast<unsigned>(frame.size));
        lastFrameLogMs = now;
    }
    videoReceiver.releaseFrame(frame);
}

void handlePortalProbe() {
    // Captive-network probes expect a non-success response or portal content.
    // A redirect encourages phones and computers to open their captive portal UI.
    redirectToPortal();
}
} // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("[VIDEO TERMINAL] boot");

    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);

    if (!WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET)) {
        Serial.println("[WIFI] softAPConfig failed");
    }

    const bool apStarted = WiFi.softAP(AP_SSID);
    Serial.printf("[WIFI] softAP start: %s\n", apStarted ? "OK" : "FAILED");
    Serial.print("[WIFI] AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.printf("[WIFI] connected clients: %u\n", WiFi.softAPgetStationNum());

    const bool dnsStarted = dnsServer.start(DNS_PORT, "*", AP_IP);
    Serial.printf("[DNS] captive DNS start: %s, port=%u, wildcard=* -> %s\n",
                  dnsStarted ? "OK" : "FAILED", DNS_PORT, AP_IP.toString().c_str());

    if (MDNS.begin(MDNS_NAME)) {
        Serial.println("[mDNS] http://openmv-wifi.local/");
    } else {
        Serial.println("[mDNS] start failed; captive DNS remains enabled");
    }

    if (!videoReceiver.begin()) {
        Serial.println("[VIDEO RX] begin failed");
        while (true) {
            dnsServer.processNextRequest();
            delay(1000);
        }
    }

    server.on("/", HTTP_GET, handleRoot);
    server.on("/health", HTTP_GET, handleHealth);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/frame.jpg", HTTP_GET, handleFrame);

    // Common captive-portal detection endpoints used by Android, Apple,
    // Windows and other clients. They all redirect to the local home page.
    server.on("/generate_204", HTTP_ANY, handlePortalProbe);
    server.on("/gen_204", HTTP_ANY, handlePortalProbe);
    server.on("/hotspot-detect.html", HTTP_ANY, handlePortalProbe);
    server.on("/library/test/success.html", HTTP_ANY, handlePortalProbe);
    server.on("/success.txt", HTTP_ANY, handlePortalProbe);
    server.on("/connecttest.txt", HTTP_ANY, handlePortalProbe);
    server.on("/ncsi.txt", HTTP_ANY, handlePortalProbe);
    server.on("/canonical.html", HTTP_ANY, handlePortalProbe);
    server.on("/redirect", HTTP_ANY, handlePortalProbe);
    server.on("/fwlink", HTTP_ANY, handlePortalProbe);
    server.on("/mobile/status.php", HTTP_ANY, handlePortalProbe);
    server.onNotFound(redirectToPortal);

    server.begin();
    Serial.println("[VIDEO RX] SPI2 slave ready: SCK=4 MISO=5 MOSI=6 CS=7");
    Serial.println("[HTTP] server ready on port 80");
    Serial.println("[HTTP] captive portal: DNS wildcard + HTTP redirect enabled");
    Serial.println("[HTTP] open http://192.168.4.1/ if the portal does not pop up automatically");
}

void loop() {
    dnsServer.processNextRequest();
    server.handleClient();
    delay(2);
}
