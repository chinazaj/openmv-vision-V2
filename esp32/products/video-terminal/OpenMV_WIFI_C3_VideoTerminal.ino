/*
 * OpenMV + ESP32-C3 Video Terminal
 *
 * Migration rule:
 *   This product entry is migrated from the validated
 *   OpenMV_WIFI_C3_V0_1.ino implementation in the original repository.
 *   Product code owns Wi-Fi/Web/UI composition; SPI video reception is
 *   provided by the shared ESP32 core SpiVideoReceiver.
 *
 * The SPI V2 protocol and runtime behavior are intentionally not redesigned
 * here. The receiver is the migrated implementation of the original
 * task_spi_receive() path.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_http_server.h>

#include "html_common.h"
#include "html_root.h"
#include "html_wifi.h"
#include "html_video.h"

#include "../../core/video/spi_video_receiver.h"
// Arduino IDE does not compile this external Core .cpp automatically.
// Include the implementation once here, matching the previously working sketch build.
#include "../../core/video/spi_video_receiver.cpp"

namespace {

constexpr char AP_SSID[] = "openmv-wifi";
constexpr char MDNS_NAME[] = "openmv-wifi";
constexpr uint8_t DNS_PORT = 53;
constexpr uint8_t GPIO_LED = 10;

constexpr uint8_t OPENMV_UART_RX = 0;
constexpr uint8_t OPENMV_UART_TX = 1;
constexpr uint32_t OPENMV_UART_BAUD = 115200;

const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_GATEWAY(192, 168, 4, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

WebServer server(80);
DNSServer dnsServer;
httpd_handle_t streamHttpServer = nullptr;
Preferences prefs;
HardwareSerial OpenMVUart(1);
SpiVideoReceiver videoReceiver;

bool ledEnabled = true;
bool videoActive = false;
String cachedSsid;
unsigned long lastReconnectAttempt = 0;
constexpr unsigned long RECONNECT_INTERVAL_MS = 10000;

String compactJsonWhitespace(const String& input) {
    String output;
    output.reserve(input.length());
    bool inString = false;
    bool escaped = false;

    for (size_t i = 0; i < input.length(); ++i) {
        const char ch = input.charAt(i);
        if (inString) {
            output += ch;
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
        } else if (ch == '"') {
            inString = true;
            output += ch;
        } else if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            output += ch;
        }
    }
    return output;
}

void setVideoState(bool enabled) {
    videoActive = enabled;
    prefs.putBool("video_en", enabled);
    Serial.printf("[VIDEO] state=%s\n", enabled ? "ON" : "OFF");
}

void startMdns() {
    if (MDNS.begin(MDNS_NAME)) {
        Serial.printf("[mDNS] http://%s.local/\n", MDNS_NAME);
    } else {
        Serial.println("[mDNS] start failed");
    }
}

void startApProvisioning() {
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);

    if (!WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET)) {
        Serial.println("[WiFi] softAPConfig failed");
    }

    const bool started = WiFi.softAP(AP_SSID, "12345678");
    Serial.printf("[WiFi] AP start: %s\n", started ? "OK" : "FAILED");
    Serial.print("[WiFi] AP IP: ");
    Serial.println(WiFi.softAPIP());

    const bool dnsStarted = dnsServer.start(DNS_PORT, "*", AP_IP);
    Serial.printf("[DNS] captive DNS: %s\n", dnsStarted ? "OK" : "FAILED");
    startMdns();
}

void redirectToPortal() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain; charset=utf-8", "Redirecting to OpenMV Video Terminal");
}

void handleRoot() {
    const bool connected = WiFi.status() == WL_CONNECTED;
    const String networkMode = connected ? "STA 客户端" : "AP 热点";
    const String ipAddress = connected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    const String videoState = videoActive ? "传输中" : "已停止";

    String html = String(HTML_HEAD) + HTML_ROOT_RELEASE_BODY;
    html.replace("{{NETWORK_MODE}}", networkMode);
    html.replace("{{IP_ADDRESS}}", ipAddress);
    html.replace("{{VIDEO_STATE}}", videoState);
    html += "</body></html>";

    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.send(200, "text/html; charset=utf-8", html);
}

void handleWifiPage() {
    String html = String(HTML_HEAD);
    html += HTML_WIFI_BODY;
    html += "</body></html>";
    server.send(200, "text/html; charset=utf-8", html);
}

void handleSaveWifi() {
    if (!server.hasArg("ssid") || !server.hasArg("pass")) {
        server.send(400, "text/plain; charset=utf-8", "Bad Request");
        return;
    }

    // Keep the original form contract and NVS keys used by the stable firmware.
    const String ssid = server.arg("ssid");
    const String password = server.arg("pass");
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);

    const String savedSsid = prefs.getString("ssid", "");
    const String savedPassword = prefs.getString("pass", "");
    if (savedSsid != ssid || savedPassword != password) {
        Serial.println("[WiFi] ERROR: NVS read-back did not match submitted credentials");
        server.send(500, "text/plain; charset=utf-8", "Wi-Fi settings could not be saved. Please retry.");
        return;
    }

    Serial.printf("[WiFi] Credentials persisted and verified; SSID length=%u\n",
                  static_cast<unsigned>(ssid.length()));

    String html = String(HTML_HEAD);
    html += "<div class='card'><h3>Wi-Fi 信息已保存！设备正在重启...</h3></div></body></html>";
    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", html);
    server.client().flush();
    delay(2000);
    ESP.restart();
}

void handleClearWifi() {
    prefs.remove("ssid");
    prefs.remove("pass");
    WiFi.disconnect(true, true);

    String html = String(HTML_HEAD);
    html += "<div class='card'><h3>Wi-Fi 配置已清除，设备正在重启...</h3></div></body></html>";
    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", html);
    server.client().flush();
    delay(1500);
    ESP.restart();
}

esp_err_t streamHandler(httpd_req_t* request) {
    httpd_resp_set_type(request, "multipart/x-mixed-replace; boundary=frame");
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(request, "Pragma", "no-cache");
    httpd_resp_set_hdr(request, "Connection", "close");

    char partHeader[128];
    uint32_t lastFrameAt = millis();

    while (true) {
        SpiVideoReceiver::FrameView frame;
        if (videoReceiver.acquireLatestFrame(frame) && frame.data != nullptr && frame.size > 0) {
            snprintf(partHeader, sizeof(partHeader),
                     "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                     static_cast<unsigned>(frame.size));

            esp_err_t result = httpd_resp_send_chunk(request, partHeader, strlen(partHeader));
            if (result == ESP_OK) {
                result = httpd_resp_send_chunk(request,
                                               reinterpret_cast<const char*>(frame.data),
                                               frame.size);
            }
            if (result == ESP_OK) {
                result = httpd_resp_send_chunk(request, "\r\n", 2);
            }
            videoReceiver.releaseFrame(frame);

            if (result != ESP_OK) {
                Serial.println("[STREAM] client disconnected or send failed");
                break;
            }
            lastFrameAt = millis();
            continue;
        }

        if (millis() - lastFrameAt > 5000) {
            Serial.println("[STREAM] no new frame for 5 seconds; closing stream");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    httpd_resp_send_chunk(request, nullptr, 0);
    return ESP_OK;
}

bool startHttpStreamServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.ctrl_port = 32769;
    config.send_wait_timeout = 1;
    config.lru_purge_enable = true;

    httpd_uri_t streamUri{};
    streamUri.uri = "/stream";
    streamUri.method = HTTP_GET;
    streamUri.handler = streamHandler;
    streamUri.user_ctx = nullptr;

    if (httpd_start(&streamHttpServer, &config) != ESP_OK) {
        Serial.println("[STREAM] failed to start HTTP server on port 81");
        streamHttpServer = nullptr;
        return false;
    }
    if (httpd_register_uri_handler(streamHttpServer, &streamUri) != ESP_OK) {
        Serial.println("[STREAM] failed to register /stream");
        httpd_stop(streamHttpServer);
        streamHttpServer = nullptr;
        return false;
    }

    Serial.println("[STREAM] MJPEG stream ready at http://<device-ip>:81/stream");
    return true;
}

void handleStatus() {
    const auto stats = videoReceiver.getStats();
    String json = "{";
    json += "\"wifi_mode\":\"" + String(WiFi.getMode() == WIFI_AP ? "AP" : "STA") + "\"";
    json += ",\"wifi_clients\":" + String(WiFi.softAPgetStationNum());
    json += ",\"video_active\":" + String(videoActive ? "true" : "false");
    json += ",\"led_enabled\":" + String(ledEnabled ? "true" : "false");
    json += ",\"rx_ok_frames\":" + String(stats.ok_frames);
    json += ",\"rx_bad_frames\":" + String(stats.bad_frames);
    json += ",\"rx_bad_header_count\":" + String(stats.bad_headers);
    json += ",\"rx_dropped_frames\":" + String(stats.dropped_frames);
    json += ",\"rx_timeout_count\":" + String(stats.timeouts);
    json += ",\"rx_oversize_count\":" + String(stats.oversize);
    json += ",\"rx_frame_id_gap_count\":" + String(stats.frame_id_gaps);
    json += "}";
    server.send(200, "application/json", json);
}

void handleFrame() {
    SpiVideoReceiver::FrameView frame;
    if (!videoReceiver.acquireLatestFrame(frame)) {
        server.send(503, "text/plain; charset=utf-8", "No video frame received yet");
        return;
    }

    server.setContentLength(frame.size);
    server.send(200, "image/jpeg", "");
    const size_t written = server.client().write(frame.data, frame.size);
    if (written != frame.size) {
        Serial.printf("[VIDEO HTTP] short JPEG write: %u/%u bytes\n",
                      static_cast<unsigned>(written),
                      static_cast<unsigned>(frame.size));
    }
    videoReceiver.releaseFrame(frame);
}

void handleNotFound() {
    if (WiFi.getMode() == WIFI_AP) {
        redirectToPortal();
    } else {
        server.send(404, "text/plain", "Not Found");
    }
}

void setupWebServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/wifi", HTTP_GET, handleWifiPage);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/frame.jpg", HTTP_GET, handleFrame);

    server.on("/start", HTTP_GET, []() {
        setVideoState(true);
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "Starting video");
    });

    server.on("/stop", HTTP_GET, []() {
        setVideoState(false);
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "Stopping video");
    });

    server.on("/toggle_led", HTTP_GET, []() {
        ledEnabled = !ledEnabled;
        prefs.putBool("led_en", ledEnabled);
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "LED updated");
    });

    server.on("/save_wifi", HTTP_POST, handleSaveWifi);
    server.on("/clear_wifi", HTTP_POST, handleClearWifi);

    server.on("/generate_204", HTTP_GET, redirectToPortal);
    server.on("/gen_204", HTTP_GET, redirectToPortal);
    server.on("/hotspot-detect.html", HTTP_GET, redirectToPortal);
    server.on("/library/test/success.html", HTTP_GET, redirectToPortal);
    server.on("/success.txt", HTTP_GET, redirectToPortal);
    server.on("/connecttest.txt", HTTP_GET, redirectToPortal);
    server.on("/ncsi.txt", HTTP_GET, redirectToPortal);
    server.on("/canonical.html", HTTP_GET, redirectToPortal);
    server.on("/redirect", HTTP_GET, redirectToPortal);
    server.on("/fwlink", HTTP_GET, redirectToPortal);
    server.onNotFound(handleNotFound);
    server.begin();

    Serial.println("[HTTP] server ready on port 80");
    Serial.println("[HTTP] GET /api/status");
    Serial.println("[HTTP] GET /frame.jpg");
    Serial.println("[HTTP] GET /start /stop /toggle_led");
}

void handleOpenMvUart() {
    static String line;
    while (OpenMVUart.available()) {
        const int ch = OpenMVUart.read();
        if (ch < 0) break;
        if (ch == '\n') {
            line.trim();
            if (line.length() == 0) {
                line = "";
                continue;
            }

            const String compact = compactJsonWhitespace(line);
            if (compact.indexOf("\"video_enabled\":true") >= 0) {
                videoActive = true;
                prefs.putBool("video_en", true);
                Serial.println("[UART] OpenMV confirms video_enabled=true");
            } else if (compact.indexOf("\"video_enabled\":false") >= 0) {
                videoActive = false;
                prefs.putBool("video_en", false);
                Serial.println("[UART] OpenMV confirms video_enabled=false");
            } else {
                Serial.printf("[UART] OpenMV: %s\n", line.c_str());
            }
            line = "";
        } else if (ch != '\r') {
            if (line.length() < 4096) {
                line += static_cast<char>(ch);
            } else {
                line = "";
                Serial.println("[UART] dropped overlong line");
            }
        }
    }
}

void setupWifi() {
    cachedSsid = prefs.getString("ssid", "");
    const String password = prefs.getString("pass", "");

    if (cachedSsid.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(cachedSsid.c_str(), password.c_str());
        Serial.printf("[WiFi] Connecting to %s", cachedSsid.c_str());

        int retries = 0;
        while (WiFi.status() != WL_CONNECTED && retries < 20) {
            delay(500);
            Serial.print('.');
            ++retries;
        }
        Serial.println();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.printf("[WiFi] STA connect failed, status=%d; falling back to AP provisioning\\n", static_cast<int>(WiFi.status()));
        }
    }

    if (WiFi.status() != WL_CONNECTED) {
        startApProvisioning();
    } else {
        Serial.print("[WiFi] IP: ");
        Serial.println(WiFi.localIP());
        startMdns();
    }
}

void updateLed() {
    static unsigned long lastBlink = 0;
    static bool ledState = LOW;

    if (videoActive && ledEnabled) {
        if (millis() - lastBlink >= 250) {
            lastBlink = millis();
            ledState = !ledState;
            digitalWrite(GPIO_LED, ledState);
        }
    } else {
        digitalWrite(GPIO_LED, LOW);
        ledState = LOW;
    }
}

void printStats() {
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint < 5000) return;
    lastPrint = millis();

    const auto stats = videoReceiver.getStats();
    Serial.printf("[STAT] rx_ok=%lu bad=%lu hdr=%lu drop=%lu timeout=%lu oversize=%lu frame_gap=%lu heap=%u min_heap=%u\n",
                  static_cast<unsigned long>(stats.ok_frames),
                  static_cast<unsigned long>(stats.bad_frames),
                  static_cast<unsigned long>(stats.bad_headers),
                  static_cast<unsigned long>(stats.dropped_frames),
                  static_cast<unsigned long>(stats.timeouts),
                  static_cast<unsigned long>(stats.oversize),
                  static_cast<unsigned long>(stats.frame_id_gaps),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()));
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("==================================================");
    Serial.println("[BOOT] OpenMV + ESP32-C3 Video Terminal");
    Serial.printf("[BOOT] UART1 RX=%u TX=%u baud=%lu\n",
                  OPENMV_UART_RX, OPENMV_UART_TX,
                  static_cast<unsigned long>(OPENMV_UART_BAUD));

    OpenMVUart.begin(OPENMV_UART_BAUD, SERIAL_8N1,
                     OPENMV_UART_RX, OPENMV_UART_TX);
    OpenMVUart.setTimeout(10);

    pinMode(GPIO_LED, OUTPUT);
    digitalWrite(GPIO_LED, LOW);

    prefs.begin("sys_cfg", false);
    ledEnabled = prefs.getBool("led_en", true);
    videoActive = prefs.getBool("video_en", false);

    setupWifi();

    if (!videoReceiver.begin()) {
        Serial.println("[VIDEO RX] begin failed");
        while (true) {
            if (WiFi.getMode() == WIFI_AP) dnsServer.processNextRequest();
            delay(1000);
        }
    }

    setupWebServer();
    startHttpStreamServer();

    Serial.println("[VIDEO RX] SPI2 slave ready: SCK=4 MISO=5 MOSI=6 CS=7");
    Serial.println("[VIDEO RX] migrated from validated OpenMV_WIFI_C3_V0_1 receive path");
    Serial.println("[HTTP] open http://192.168.4.1/ when using AP mode");
    Serial.println("[HTTP] video stream is served on port 81 at /stream");
}

void loop() {
    if (WiFi.getMode() == WIFI_AP) {
        dnsServer.processNextRequest();
    }

    server.handleClient();
    handleOpenMvUart();

    if (cachedSsid.length() > 0 && WiFi.status() != WL_CONNECTED) {
        const unsigned long now = millis();
        if (now - lastReconnectAttempt >= RECONNECT_INTERVAL_MS) {
            lastReconnectAttempt = now;
            Serial.println("[WiFi] Reconnecting...");
            WiFi.disconnect();
            WiFi.reconnect();
        }
    }

    updateLed();
    printStats();
    delay(2);
}
