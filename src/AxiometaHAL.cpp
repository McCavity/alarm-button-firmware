#include "AxiometaHAL.h"
#include <Arduino.h>
#include <SPI.h>

using namespace alarmcore;

// Confirmed pins (CLAUDE.md "Module layout (confirmed 2026-06-27)").
static const uint8_t PIN_LCD_MOSI = 12, PIN_LCD_SCK = 14;
static const uint8_t PIN_LCD_CS = 4, PIN_LCD_DC = 2, PIN_LCD_RST = 3;
static const uint8_t PIN_BUZZER = 6;
static const uint8_t PIN_BTN = 16;     // active-low (INPUT_PULLUP)
static const uint8_t PIN_LED = 15;     // active-high
static const uint8_t PIN_ENC_A = 17, PIN_ENC_B = 18, PIN_ENC_PUSH = 1;

static const uint16_t COL_ORANGE = 0xFD20;  // 565 orange (no named constant)

AxiometaHAL::AxiometaHAL() : tft_(PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST) {}

void AxiometaHAL::init() {
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_ENC_PUSH, INPUT_PULLUP);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  SPI.begin(PIN_LCD_SCK, -1, PIN_LCD_MOSI, -1);
  tft_.initR(INITR_MINI160x80);
  tft_.invertDisplay(false);
  tft_.setRotation(3);
  tft_.setTextWrap(false);
  tft_.fillScreen(ST77XX_BLACK);
  lastSig_.clear();
}

void AxiometaHAL::tick() {
  uint32_t now = millis();

  // Button (ack): active-low. A confirmed transition to "pressed" = a press event.
  bool btnRaw = (digitalRead(PIN_BTN) == LOW);
  if (ackDeb_.update(btnRaw, now) && ackDeb_.state()) ackEvent_ = true;

  // Encoder rotation.
  bool a = (digitalRead(PIN_ENC_A) == LOW);
  bool b = (digitalRead(PIN_ENC_B) == LOW);
  navAccum_ += quad_.update(a, b);

  // Encoder push: ACTIVE-HIGH (idle LOW, pressed HIGH — confirmed bring-up 2026-06-27),
  // unlike the active-low ack button. Debounce, then classify short/long.
  bool pushRaw = (digitalRead(PIN_ENC_PUSH) == HIGH);
  pushDeb_.update(pushRaw, now);
  switch (press_.update(pushDeb_.state(), now)) {
    case PressEvent::SHORT_CLICK: detailEvent_ = true; break;
    case PressEvent::LONG_PRESS:  muteEvent_ = true; break;
    case PressEvent::NONE: break;
  }

  updateLed();
  updateSound(now);
}

bool AxiometaHAL::acknowledgePressed() { bool e = ackEvent_; ackEvent_ = false; return e; }
int  AxiometaHAL::navDelta()           { int d = navAccum_; navAccum_ = 0; return d; }
bool AxiometaHAL::muteTogglePressed()  { bool e = muteEvent_; muteEvent_ = false; return e; }
bool AxiometaHAL::detailTogglePressed(){ bool e = detailEvent_; detailEvent_ = false; return e; }

void AxiometaHAL::setStatusLed(StatusLedMode mode) { ledMode_ = mode; }

void AxiometaHAL::updateLed() {
  bool on = false;
  switch (ledMode_) {
    case StatusLedMode::OFF:        on = false; break;
    case StatusLedMode::SOLID:      on = true; break;
    case StatusLedMode::BLINK_SLOW: on = (millis() / 500) % 2; break;
    case StatusLedMode::BLINK_FAST: on = (millis() / 150) % 2; break;
  }
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

void AxiometaHAL::playAlertSound(AlertSound level) { soundMode_ = level; }

void AxiometaHAL::updateSound(uint32_t now) {
  switch (soundMode_) {
    case AlertSound::URGENT:
      if (now - lastBeepMs_ >= 600) { tone(PIN_BUZZER, 3200, 200); lastBeepMs_ = now; }
      break;
    case AlertSound::SHORT_BEEP:
      tone(PIN_BUZZER, 2700, 120); soundMode_ = AlertSound::OFF;   // single chirp
      break;
    case AlertSound::OFF:
    default:
      noTone(PIN_BUZZER);
      break;
  }
}

uint16_t AxiometaHAL::severityColor(const std::string& sev) const {
  if (sev == "critical") return ST77XX_RED;
  if (sev == "warning")  return COL_ORANGE;
  return ST77XX_WHITE;   // info / unknown
}

void AxiometaHAL::showAlarmList(const std::vector<std::string>& lines, int selectedIdx,
                                const std::string& maxSeverity) {
  std::string sig = "L|" + std::to_string(selectedIdx) + "|" + maxSeverity;
  for (const auto& l : lines) sig += "|" + l;
  if (sig == lastSig_) return;     // unchanged -> skip redraw (no flicker)
  lastSig_ = sig;

  tft_.fillScreen(ST77XX_BLACK);
  tft_.setTextSize(1);

  tft_.setCursor(2, 2);
  tft_.setTextColor(lines.empty() ? ST77XX_GREEN : severityColor(maxSeverity));
  tft_.printf("ALARMS %d", (int)lines.size());
  tft_.drawFastHLine(0, 12, tft_.width(), ST77XX_WHITE);

  int y = 16;
  const int rowH = 9;
  int maxRows = (tft_.height() - y) / rowH;
  for (int i = 0; i < (int)lines.size() && i < maxRows; i++) {
    bool sel = (i == selectedIdx);
    if (sel) tft_.fillRect(0, y - 1, tft_.width(), rowH, ST77XX_WHITE);
    tft_.setTextColor(sel ? ST77XX_BLACK : ST77XX_WHITE);
    tft_.setCursor(2, y);
    tft_.print(sel ? ">" : " ");
    tft_.print(lines[i].substr(0, 25).c_str());
    y += rowH;
  }

  // connection-OK dot (the list screen is only shown when the link is up)
  tft_.fillCircle(tft_.width() - 5, tft_.height() - 5, 2, ST77XX_GREEN);
}

void AxiometaHAL::showAlarmDetail(const std::string& text) {
  std::string sig = "D|" + text;
  if (sig == lastSig_) return;
  lastSig_ = sig;

  tft_.fillScreen(ST77XX_BLACK);
  tft_.setTextSize(1);
  tft_.setTextWrap(true);
  tft_.setTextColor(ST77XX_WHITE);
  tft_.setCursor(2, 2);
  tft_.print(text.c_str());
  tft_.setTextWrap(false);
}

void AxiometaHAL::showStatus(const std::string& line) {
  std::string sig = "S|" + line;
  if (sig == lastSig_) return;
  lastSig_ = sig;

  tft_.fillScreen(ST77XX_BLACK);
  tft_.setTextSize(2);
  tft_.setTextColor(ST77XX_YELLOW);
  tft_.setCursor(4, 28);
  tft_.print(line.c_str());
  tft_.setTextSize(1);
}
