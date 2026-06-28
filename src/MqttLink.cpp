#include "MqttLink.h"
#include "parser.h"
#include "secrets.h"
#include <time.h>

MqttLink* MqttLink::instance_ = nullptr;

void MqttLink::buildClientId() {
  uint64_t mac = ESP.getEfuseMac();           // unique per board
  uint8_t a = (mac >> 16) & 0xFF, b = (mac >> 8) & 0xFF, c = mac & 0xFF;
  char buf[40];
  snprintf(buf, sizeof(buf), "alarmbutton-%s-%02X%02X%02X", DEVICE_ID, a, b, c);
  clientId_ = buf;                             // never collides with btn_*
}

void MqttLink::begin() {
  instance_ = this;
  buildClientId();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);            // non-blocking; loop() drives the rest
  configTime(0, 0, "pool.ntp.org");            // SNTP, UTC; never wait on it
  client_.begin(MQTT_HOST, MQTT_PORT, net_);
  client_.onMessage(onMqttMessage);
  Serial.printf("[mqtt] client id %s\n", clientId_.c_str());
}

void MqttLink::ensureWifi() {
  bool nowConnected = (WiFi.status() == WL_CONNECTED);
  // On a disconnected->connected transition, clear the MQTT backoff so the
  // next ensureMqtt() reconnects immediately instead of waiting out a stale guard.
  if (nowConnected && !wifiWasConnected_) lastReconnectMs_ = 0;
  wifiWasConnected_ = nowConnected;
  // WiFi.begin already called in begin(); the ESP auto-retries. Nothing to block on.
}

void MqttLink::ensureMqtt() {
  if (client_.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastReconnectMs_ < 2000) return;   // backoff: retry every 2 s
  lastReconnectMs_ = now;
  Serial.print("[mqtt] connecting... ");
  if (client_.connect(clientId_.c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.println("ok");
    subscribeTopics();
  } else {
    Serial.printf("failed (rc=%d)\n", (int)client_.lastError());
  }
}

void MqttLink::subscribeTopics() {
  client_.subscribe("alarmbutton/" DEVICE_ID "/list", 1);
  client_.subscribe("alarmbutton/" DEVICE_ID "/new", 1);
  client_.subscribe("alarmbutton/" DEVICE_ID "/heartbeat", 1);
}

void MqttLink::loop() {
  ensureWifi();
  ensureMqtt();
  client_.loop();
}

void MqttLink::onMqttMessage(String& topic, String& payload) {
  if (instance_) instance_->handleMessage(topic, payload);
}

// Filled in Task 3:
void MqttLink::handleMessage(String& topic, String& payload) { (void)topic; (void)payload; }

// Filled in Task 5:
bool MqttLink::isoTimeUtc(char* out, size_t n) { (void)out; (void)n; return false; }
void MqttLink::publishAck(const char* action) { (void)action; }
