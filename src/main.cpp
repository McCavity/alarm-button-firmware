#include <Arduino.h>
#include "AxiometaHAL.h"
#include <string>
#include <vector>

using namespace alarmcore;

// Phase 0c HAL self-test: render a static list, blink the LED, beep once. Proves the
// ST7735/SPI wiring, the LED, and the buzzer in real firmware before the interactive demo.
static AxiometaHAL hal;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("phase 0c HAL self-test — static list + LED blink + one beep");
  hal.init();
  std::vector<std::string> lines = {"nutapp01 self-test", "nasapp01 ok", "dump1090 ok"};
  hal.showAlarmList(lines, 0, "critical");     // red header, row 0 selected
  hal.setStatusLed(StatusLedMode::BLINK_FAST);
  hal.playAlertSound(AlertSound::SHORT_BEEP);
}

void loop() {
  hal.tick();    // advances the LED blink
  delay(5);
}
