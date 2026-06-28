#pragma once
#include <WiFi.h>
#include <MQTT.h>
#include <functional>
#include "contract.h"

// Hardware-side WiFi + MQTT link for the alarm button. Owns the connection,
// routes incoming payloads through the core parsers into callbacks, and
// publishes acks. The core (lib/alarmcore) stays hardware-free; this glue lives in src/.
class MqttLink {
public:
  using ListCb      = std::function<void(const alarmcore::ListPayload&)>;
  using NewCb       = std::function<void(const alarmcore::NewPayload&)>;
  using HeartbeatCb = std::function<void(const alarmcore::Heartbeat&)>;

  void begin();
  void loop();
  bool isConnected() { return client_.connected(); }
  unsigned long lastHeartbeatMs() const { return lastHeartbeatMs_; }

  void onList(ListCb cb)           { listCb_ = cb; }
  void onNew(NewCb cb)             { newCb_ = cb; }
  void onHeartbeat(HeartbeatCb cb) { hbCb_ = cb; }

  void publishAck(const char* action);

private:
  void ensureWifi();
  void ensureMqtt();
  void subscribeTopics();
  void handleMessage(String& topic, String& payload);
  void buildClientId();
  bool isoTimeUtc(char* out, size_t n);   // false if SNTP not synced yet

  static void onMqttMessage(String& topic, String& payload);  // trampoline
  static MqttLink* instance_;

  WiFiClient net_;
  MQTTClient client_{8192};               // >= 8 KB buffer (worst-case list)
  String clientId_;
  unsigned long lastReconnectMs_ = 0;
  unsigned long lastHeartbeatMs_ = 0;

  ListCb listCb_;
  NewCb  newCb_;
  HeartbeatCb hbCb_;
};
