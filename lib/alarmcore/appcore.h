#pragma once
#include "contract.h"
#include "view.h"
#include <string>
#include <vector>

// UI state machine for the "dumb" button. Holds the latest contract payloads plus the
// interactive state (selection, detail view, mute) and emits one RenderModel per frame by
// composing the pure computeView(). Hardware-free / host-testable; the HAL renders it.
namespace alarmcore {

enum class Screen { LIST, DETAIL, STATUS };

struct RenderModel {
  LedMode led = LedMode::OFF;
  bool beep = false;                 // one-shot; already gated by mute
  Screen screen = Screen::LIST;
  std::vector<std::string> lines;    // LIST: "host name" per alarm
  int selectedIdx = 0;               // LIST
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

  RenderModel render();                         // consumes the one-shot new event

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
  bool        ackPending_ = false;
  std::string ackId_;
};

} // namespace alarmcore
