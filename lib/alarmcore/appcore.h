#pragma once
#include "contract.h"
#include "view.h"
#include "scroll.h"
#include "hal.h"
#include <string>
#include <vector>

// UI state machine for the "dumb" button. Holds the latest contract payloads plus the
// interactive state (selection, detail view, mute) and emits one RenderModel per frame by
// composing the pure computeView(). Hardware-free / host-testable; the HAL renders it.
namespace alarmcore {

enum class Screen { LIST, DETAIL, STATUS };

struct RenderModel {
  LedMode led = LedMode::OFF;
  AlertSound sound = AlertSound::OFF;   // OFF | URGENT (sustained, gated by mute)
  Screen screen = Screen::LIST;
  std::vector<Row> rows;             // LIST: structured alarm rows
  int critCount = 0, warnCount = 0, omitted = 0, total = 0;
  int selectedIdx = 0;               // LIST
  int scrollTop = 0;                 // first visible row index
  int count = 0;
  std::string maxSeverity;           // LIST header colour
  std::string detailText;            // DETAIL
  std::string statusText;            // STATUS
};

class AppCore {
public:
  // data inputs (from MQTT later; from canned data in the demo)
  void setList(const ListPayload& list);
  void onNew(const NewPayload& n);            // one-shot, consumed by next render()
  void onHeartbeat(const Heartbeat& hb, bool stale);

  // events (from the HAL)
  void nav(int delta);
  void toggleDetail();
  void toggleMute();
  void acknowledge();

  bool takeAckOne(std::string& id);            // true once after a press; fills focused fingerprint
  bool muted() const { return muted_; }

  RenderModel render(uint32_t nowMs = 0);       // consumes the one-shot new event; nowMs drives the alert window

private:
  void clampSelection();
  std::string detailText() const;
  std::string focusId() const;              // fingerprint under the cursor in the current list
  int firstUnacked() const;                 // index of first !acked alarm, or -1
  void reconcileFocus(const std::string& prevId);  // re-locate cursor after a list update

  ListPayload list_;
  NewPayload  new_;
  bool        newPending_ = false;
  Heartbeat   hb_;
  bool        stale_ = false;
  int         selectedIdx_ = 0;
  bool        detail_ = false;
  bool        muted_ = false;
  uint32_t    urgentUntilMs_ = 0;          // urgent-sound window deadline (0 = not sounding)
  bool        ackPending_ = false;
  std::string ackId_;
  int         scrollTop_ = 0;              // cached scroll position (hysteresis)
};

} // namespace alarmcore
