#pragma once
#include <string>
#include <vector>

// Hardware abstraction layer (design spec section 2, design-2026-05-16-mini in the KI-OS vault).
// The core only knows this interface; implementations:
//   AxiometaHAL (ESP32-S3 Genesis Mini, active) · LilyGoHAL (parked) · MockHAL (host tests).
// The HAL exposes ABSTRACT events (navDelta, acknowledgePressed, …), not "encoder rotation",
// so the LiliGo 3-button variant fits underneath the same core.
namespace alarmcore {

enum class StatusLedMode { OFF, SOLID, BLINK_SLOW, BLINK_FAST };
enum class AlertSound { OFF, SHORT_BEEP, URGENT };

class AlarmButtonHAL {
public:
  virtual ~AlarmButtonHAL() = default;
  virtual void init() = 0;
  virtual void tick() = 0;   // per loop iteration: poll + debounce inputs

  // Inputs — event producers
  virtual bool acknowledgePressed() = 0;
  virtual int  navDelta() = 0;            // -1 / 0 / +1 per tick
  virtual bool muteTogglePressed() = 0;
  virtual bool detailTogglePressed() = 0;

  // Outputs — actuators
  virtual void setStatusLed(StatusLedMode mode) = 0;
  virtual void playAlertSound(AlertSound level) = 0;
  virtual void showAlarmList(const std::vector<std::string>& lines, int selectedIdx,
                             const std::string& maxSeverity) = 0;
  virtual void showAlarmDetail(const std::string& text) = 0;
  virtual void showStatus(const std::string& line) = 0;
};

} // namespace alarmcore
