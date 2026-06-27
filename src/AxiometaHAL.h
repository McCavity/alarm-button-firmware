#pragma once
#include "hal.h"
#include "debounce.h"
#include "quadrature.h"
#include "pressclassifier.h"
#include <Adafruit_ST7735.h>
#include <string>
#include <vector>

// Hardware implementation of AlarmButtonHAL for the Axiometa Genesis Mini.
// Pins are the confirmed bring-up values (see CLAUDE.md "Module layout").
class AxiometaHAL : public alarmcore::AlarmButtonHAL {
public:
  AxiometaHAL();
  void init() override;
  void tick() override;

  bool acknowledgePressed() override;        // consume-on-read
  int  navDelta() override;                   // consume-on-read
  bool muteTogglePressed() override;          // consume-on-read
  bool detailTogglePressed() override;        // consume-on-read

  void setStatusLed(alarmcore::StatusLedMode mode) override;
  void playAlertSound(alarmcore::AlertSound level) override;
  void showAlarmList(const std::vector<std::string>& lines, int selectedIdx,
                     const std::string& maxSeverity) override;
  void showAlarmDetail(const std::string& text) override;
  void showStatus(const std::string& line) override;

private:
  void updateLed();
  uint16_t severityColor(const std::string& sev) const;

  Adafruit_ST7735 tft_;

  alarmcore::Debouncer         ackDeb_{5};
  alarmcore::Debouncer         pushDeb_{5};
  alarmcore::QuadratureDecoder quad_{2};   // this encoder yields 2 sub-steps/detent (bench 2026-06-27)
  alarmcore::PressClassifier   press_{1500};

  // latched abstract events (consume-on-read)
  bool ackEvent_ = false;
  int  navAccum_ = 0;
  bool muteEvent_ = false;
  bool detailEvent_ = false;

  alarmcore::StatusLedMode ledMode_ = alarmcore::StatusLedMode::OFF;
  std::string lastSig_;   // last rendered screen signature (skip redundant redraws)
};
