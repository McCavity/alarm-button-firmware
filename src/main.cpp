#include <Arduino.h>

// Axiometa-Mini Firmware-Entry. Phase 0b (nächste, Hardware-Session): WiFi + MQTT-Client
// (subscribe alarmbutton/office/{list,new,heartbeat}) + AxiometaHAL + Render-Loop über
// den host-getesteten Core (lib/alarmcore: parser + view). Hier bewusst noch leer.
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("alarm-button-firmware — boot (Phase 0b folgt)");
}

void loop() {
  delay(1000);
}
