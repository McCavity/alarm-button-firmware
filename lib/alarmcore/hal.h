#pragma once
#include <string>
#include <vector>

// Hardware-Abstraction-Layer (Design-Spec §2, design-2026-05-16-mini im KI-OS-Vault).
// Der Core kennt nur dieses Interface; Implementierungen:
//   AxiometaHAL (ESP32-S3 Genesis Mini, aktiv) · LilyGoHAL (parkiert) · MockHAL (Host-Tests).
// HAL liefert ABSTRAKTE Events (navDelta, acknowledgePressed …), nicht "Encoder-Drehung",
// damit die LiliGo-3-Tasten-Variante darunter passt.
namespace alarmcore {

enum class StatusLedMode { OFF, SOLID, BLINK_SLOW, BLINK_FAST };
enum class AlertSound { OFF, SHORT_BEEP, URGENT };

class AlarmButtonHAL {
public:
  virtual ~AlarmButtonHAL() = default;
  virtual void init() = 0;
  virtual void tick() = 0;   // pro Loop-Iteration: Inputs pollen + entprellen

  // Inputs — Event-Producer
  virtual bool acknowledgePressed() = 0;
  virtual int  navDelta() = 0;            // -1 / 0 / +1 pro tick
  virtual bool muteTogglePressed() = 0;
  virtual bool detailTogglePressed() = 0;

  // Outputs — Aktoren
  virtual void setStatusLed(StatusLedMode mode) = 0;
  virtual void playAlertSound(AlertSound level) = 0;
  virtual void showAlarmList(const std::vector<std::string>& lines, int selectedIdx) = 0;
  virtual void showAlarmDetail(const std::string& text) = 0;
  virtual void showStatus(const std::string& line) = 0;
};

} // namespace alarmcore
