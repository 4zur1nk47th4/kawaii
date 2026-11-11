/*
  Kawaii ESP32-CAM  (AI-Thinker ESP32-CAM + OV3660)

  What it does
  ------------
  1. Opens its own Wi-Fi hotspot   SSID: tka86   Password: 113333555555
     (a captive Wi-Fi Manager page pops up when you connect to it)
  2. In that page you pick your home/office Wi-Fi + password, and type:
        - your Vercel website  (e.g. my-cam.vercel.app)
        - the device key       (same value as DEVICE_KEY in Vercel)
  3. The camera then joins the internet and pushes JPEG frames to your
     website (https://<site>/api/frame) and reads light / camera settings back.

  Re-open the setup hotspot any time: hold the BOOT (GPIO0) button for 3 seconds.
  It also re-opens by itself if Wi-Fi has been lost for more than 60 seconds.

  Libraries (Arduino Library Manager)
  -----------------------------------
    - WiFiManager   by tzapu          (2.0.17 or newer)
    - ArduinoJson   by Benoit Blanchon (v7.x)
    - Board package: "esp32" by Espressif (2.0.x or 3.x)

  Arduino IDE settings
  --------------------
    Board:            AI Thinker ESP32-CAM
    PSRAM:            Enabled
    Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ---------------- Setup hotspot ----------------
#define AP_NAME "tka86"
#define AP_PASS "113333555555"   // must be 8+ characters

// ---------------- Pins (AI-Thinker ESP32-CAM) ----------------
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

#define FLASH_PIN        4     // high-power flash LED
#define BOOT_BTN         0     // BOOT button (hold 3 s to reopen setup hotspot)

// ---------------- State ----------------
Preferences prefs;
String serverHost = "";        // e.g. my-cam.vercel.app
String deviceKey  = "";

WiFiClientSecure secureClient;
HTTPClient http;

bool watching = false;         // true while a browser is viewing the stream
bool lightOn = false;
int  curFramesize = -1;
int  curBrightness = 99;
int  curContrast = 99;

unsigned long wifiLostSince = 0;
unsigned long btnDownSince = 0;
int failCount = 0;

// ---------------- Helpers ----------------
void loadPrefs() {
  prefs.begin("cam", false);
  serverHost = prefs.getString("host", "");
  deviceKey  = prefs.getString("key", "");
}

String cleanHost(String h) {
  h.trim();
  h.replace("https://", "");
  h.replace("http://", "");
  while (h.endsWith("/")) h.remove(h.length() - 1);
  return h;
}

// Opens the Wi-Fi Manager. force=true always shows the portal.
bool runPortal(bool force) {
  WiFiManager wm;

  WiFiManagerParameter pHost("host", "Website (e.g. my-cam.vercel.app)", serverHost.c_str(), 80);
  WiFiManagerParameter pKey("key", "Device key (same as DEVICE_KEY in Vercel)", deviceKey.c_str(), 64);
  wm.addParameter(&pHost);
  wm.addParameter(&pKey);

  wm.setConfigPortalTimeout(180);   // portal closes after 3 min, camera retries
  wm.setConnectTimeout(20);
  wm.setTitle("Kawaii ESP32-CAM Setup");

  bool ok = force ? wm.startConfigPortal(AP_NAME, AP_PASS)
                  : wm.autoConnect(AP_NAME, AP_PASS);

  if (ok) {
    String h = cleanHost(String(pHost.getValue()));
    String k = String(pKey.getValue());
    k.trim();
    if (h.length() > 0) { serverHost = h; prefs.putString("host", serverHost); }
    if (k.length() > 0) { deviceKey = k;  prefs.putString("key", deviceKey); }
  }
  return ok;
}

bool initCamera() {
  camera_config_t config;
  memset(&config, 0, sizeof(config));
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;           // 10 (best) .. 63 (smallest)
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  if (psramFound()) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;   // always send the freshest frame
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    // OV3660 quirks: image comes out upside-down and over-saturated by default.
    s->set_vflip(s, 1);
    s->set_saturation(s, -2);
  }
  s->set_framesize(s, FRAMESIZE_QVGA);
  curFramesize = FRAMESIZE_QVGA;
  return true;
}

// Apply the JSON the server sends back: {light, framesize, brightness, contrast, watch}
void applyState(const String &payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return;

  watching = doc["watch"] | false;

  bool l = doc["light"] | false;
  if (l != lightOn) {
    lightOn = l;
    digitalWrite(FLASH_PIN, lightOn ? HIGH : LOW);
  }

  sensor_t *s = esp_camera_sensor_get();
  if (!s) return;

  int fs = doc["framesize"] | (int)FRAMESIZE_QVGA;
  if (fs != curFramesize && (fs == FRAMESIZE_QVGA || fs == FRAMESIZE_VGA || fs == FRAMESIZE_SVGA)) {
    s->set_framesize(s, (framesize_t)fs);
    curFramesize = fs;
  }

  int b = doc["brightness"] | 0;
  if (b != curBrightness) { s->set_brightness(s, b); curBrightness = b; }

  int c = doc["contrast"] | 0;
  if (c != curContrast) { s->set_contrast(s, c); curContrast = c; }
}

String url(const char *path) { return "https://" + serverHost + path; }

// Cheap heartbeat while nobody is watching
void pollState() {
  http.setReuse(true);
  http.setTimeout(8000);
  if (!http.begin(secureClient, url("/api/device"))) { failCount++; return; }
  http.addHeader("x-device-key", deviceKey);
  int code = http.GET();
  if (code == 200) {
    applyState(http.getString());
    failCount = 0;
  } else {
    Serial.printf("poll failed: %d\n", code);
    failCount++;
  }
  http.end();
}

// Capture and upload one frame; the reply carries the latest settings
void sendFrame() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { Serial.println("capture failed"); delay(200); return; }

  http.setReuse(true);
  http.setTimeout(8000);
  if (!http.begin(secureClient, url("/api/frame"))) {
    esp_camera_fb_return(fb);
    failCount++;
    return;
  }
  http.addHeader("x-device-key", deviceKey);
  http.addHeader("Content-Type", "application/octet-stream");
  int code = http.POST(fb->buf, fb->len);
  esp_camera_fb_return(fb);

  if (code == 200) {
    applyState(http.getString());
    failCount = 0;
  } else {
    Serial.printf("upload failed: %d\n", code);
    failCount++;
  }
  http.end();
}

void checkButton() {
  if (digitalRead(BOOT_BTN) == LOW) {
    if (btnDownSince == 0) btnDownSince = millis();
    if (millis() - btnDownSince > 3000) {
      btnDownSince = 0;
      Serial.println("Opening setup hotspot...");
      runPortal(true);
      ESP.restart();
    }
  } else {
    btnDownSince = 0;
  }
}

// ---------------- Arduino ----------------
void setup() {
  Serial.begin(115200);
  pinMode(FLASH_PIN, OUTPUT);
  digitalWrite(FLASH_PIN, LOW);
  pinMode(BOOT_BTN, INPUT_PULLUP);

  loadPrefs();

  if (!initCamera()) {
    Serial.println("Camera error - restarting in 5 s");
    delay(5000);
    ESP.restart();
  }

  // Connect with saved Wi-Fi, or open the "tka86" hotspot + Wi-Fi Manager.
  bool ok = runPortal(false);
  if (ok && (serverHost.length() == 0 || deviceKey.length() == 0)) {
    // Wi-Fi is fine but website/key were never entered -> show the portal.
    ok = runPortal(true);
  }
  if (!ok) {
    Serial.println("No Wi-Fi configured - restarting");
    delay(1000);
    ESP.restart();
  }

  secureClient.setInsecure();   // skips certificate check (see README for pinning)
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("Streaming to https://" + serverHost);
}

void loop() {
  checkButton();

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiLostSince == 0) wifiLostSince = millis();
    if (millis() - wifiLostSince > 60000UL) {
      runPortal(true);       // let the user pick a new network
      ESP.restart();
    }
    WiFi.reconnect();
    delay(1000);
    return;
  }
  wifiLostSince = 0;

  if (failCount >= 5) {      // drop the TLS session and start fresh
    secureClient.stop();
    failCount = 0;
    delay(1000);
  }

  if (watching) {
    sendFrame();             // as fast as the connection allows
  } else {
    pollState();
    delay(1200);
  }
}
