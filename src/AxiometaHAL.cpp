#include "AxiometaHAL.h"
#include "scroll.h"
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
      buzzerActive_ = true;
      break;
    case AlertSound::SHORT_BEEP:
      tone(PIN_BUZZER, 2700, 120); soundMode_ = AlertSound::OFF;   // single chirp (auto-expires)
      break;
    case AlertSound::OFF:
    default:
      // Stop once on the transition to OFF — NOT every idle frame: noTone() on an
      // uninitialized LEDC channel spews "LEDC is not initialized" at loop frequency.
      if (buzzerActive_) { noTone(PIN_BUZZER); buzzerActive_ = false; }
      break;
  }
}

uint16_t AxiometaHAL::severityColor(const std::string& sev) const {
  if (sev == "critical") return ST77XX_RED;
  if (sev == "warning")  return COL_ORANGE;
  return ST77XX_WHITE;   // info / unknown
}

static const uint16_t COL_GREY = 0x7BEF;

void AxiometaHAL::showAlarmList(const alarmcore::ListView& v) {
  std::string sig = "L|" + std::to_string(v.selectedIdx) + "|" + std::to_string(v.scrollTop) + "|" +
                    v.maxSeverity + "|" + std::to_string(v.total);
  for (const auto& r : v.rows) sig += "|" + r.severity + (r.acked ? "+" : "-") + r.text;
  if (sig == lastSig_) return;     // unchanged -> skip redraw (no flicker)
  lastSig_ = sig;

  tft_.fillScreen(ST77XX_BLACK);
  tft_.setTextSize(1);

  // Header: "ALARMS 22|CRIT 5|WARN 17" (24 chars; the spaced variant would need 28 of 26)
  tft_.setCursor(2, 2);
  tft_.setTextColor(v.total == 0 ? ST77XX_GREEN : severityColor(v.maxSeverity));
  tft_.printf("ALARMS %d", v.total);
  if (v.critCount > 0) { tft_.setTextColor(ST77XX_WHITE); tft_.print("|"); tft_.setTextColor(ST77XX_RED);  tft_.printf("CRIT %d", v.critCount); }
  if (v.warnCount > 0) { tft_.setTextColor(ST77XX_WHITE); tft_.print("|"); tft_.setTextColor(COL_ORANGE); tft_.printf("WARN %d", v.warnCount); }
  tft_.fillCircle(tft_.width() - 5, 5, 2, ST77XX_GREEN);   // link-OK dot, moved up (scrollbar owns the right edge)
  tft_.drawFastHLine(0, 12, tft_.width(), ST77XX_WHITE);

  const int top = 16, rowH = 9, rows = alarmcore::LIST_ROWS;
  const int n = (int)v.rows.size();
  const int virt = v.omitted > 0 ? 1 : 0;
  int y = top;
  for (int i = v.scrollTop; i < n + virt && i < v.scrollTop + rows; i++, y += rowH) {
    if (i == n) {                                   // virtual "+N more" row, never selectable
      tft_.setTextColor(COL_GREY);
      tft_.setCursor(9, y);
      tft_.printf("+%d weitere", v.omitted);
      continue;
    }
    const alarmcore::Row& r = v.rows[i];
    bool sel = (i == v.selectedIdx);
    if (sel) tft_.fillRect(0, y - 1, tft_.width() - 4, rowH, ST77XX_WHITE);
    tft_.fillCircle(4, y + 3, 2, severityColor(r.severity));
    tft_.setTextColor(sel ? ST77XX_BLACK : (r.acked ? COL_GREY : ST77XX_WHITE));
    tft_.setCursor(9, y);
    tft_.print(r.text.substr(0, 22).c_str());
    if (r.acked) {                                  // grey check: seen, NOT resolved
      uint16_t c = sel ? ST77XX_BLACK : COL_GREY;
      tft_.drawLine(146, y + 4, 148, y + 6, c);
      tft_.drawLine(148, y + 6, 152, y + 1, c);
    }
  }

  // Scrollbar (2 px, right edge) only when the list is longer than the window
  const int totalRows = n + virt;
  if (totalRows > rows) {
    const int trackY = top - 2, trackH = tft_.height() - trackY;
    int thumbH = trackH * rows / totalRows; if (thumbH < 4) thumbH = 4;
    int thumbY = trackY + trackH * v.scrollTop / totalRows;
    tft_.fillRect(tft_.width() - 2, trackY, 2, trackH, 0x2104);
    tft_.fillRect(tft_.width() - 2, thumbY, 2, thumbH, ST77XX_WHITE);
  }
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
