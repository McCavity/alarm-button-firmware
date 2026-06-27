#include <Arduino.h>

// Phase 0b — hardware bring-up smoke test (Axiometa Genesis Mini).
// Uses the on-board RGB LED + user button (no AX22 module required), so it runs on the
// bare board. GPIOs from the upstream variant pins_arduino.h (espressif/arduino-esp32,
// variant axiometa_genesis_mini):
//   on-board RGB LED (WS2812) = GPIO 21,  on-board user button = GPIO 45.
// This is intentionally NOT the final firmware — it just proves toolchain + flash + I/O.
// Next: AxiometaHAL (module LED button / buzzer / encoder / LCD) + WiFi/MQTT + the
// host-tested core (lib/alarmcore).

static const uint8_t LED_PIN = 21;  // on-board addressable RGB LED
static const uint8_t BTN_PIN = 45;  // on-board user button (active-low)

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("alarm-button-firmware — phase 0b bring-up");
  Serial.println("on-board LED (GPIO21) + button (GPIO45). Press the button.");
  pinMode(BTN_PIN, INPUT_PULLUP);
}

void loop() {
  static bool lastPressed = false;
  static bool blinkOn = false;
  static uint32_t lastBlink = 0;
  static uint32_t lastBeat = 0;

  bool pressed = (digitalRead(BTN_PIN) == LOW);

  if (millis() - lastBeat >= 1000) {           // 1 Hz serial heartbeat (proof of life)
    lastBeat = millis();
    Serial.printf("alive t=%lus  button=%s\n", millis() / 1000, pressed ? "DOWN" : "up");
  }

  if (pressed != lastPressed) {
    Serial.println(pressed ? "BUTTON pressed" : "BUTTON released");
    lastPressed = pressed;
  }

  if (pressed) {
    neopixelWrite(LED_PIN, 60, 0, 0);          // solid red while held
  } else {
    if (millis() - lastBlink >= 500) {         // idle: slow green blink (alive)
      lastBlink = millis();
      blinkOn = !blinkOn;
      neopixelWrite(LED_PIN, 0, blinkOn ? 35 : 0, 0);
    }
  }
  delay(10);
}
