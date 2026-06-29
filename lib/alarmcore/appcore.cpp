#include "appcore.h"

namespace alarmcore {

void AppCore::setList(const ListPayload& list) {
  std::string prevId = focusId();   // fingerprint under the cursor in the OLD list
  list_ = list;
  reconcileFocus(prevId);
}

std::string AppCore::focusId() const {
  if (list_.valid && selectedIdx_ >= 0 && selectedIdx_ < (int)list_.alarms.size())
    return list_.alarms[selectedIdx_].id;
  return "";
}

int AppCore::firstUnacked() const {
  for (int i = 0; i < (int)list_.alarms.size(); i++)
    if (!list_.alarms[i].acked) return i;
  return -1;
}

void AppCore::reconcileFocus(const std::string& prevId) {
  int n = list_.valid ? (int)list_.alarms.size() : 0;
  if (n == 0) { selectedIdx_ = 0; detail_ = false; return; }
  // Hold focus if the same fingerprint is still present AND still unacked (no yank on republish).
  int keep = -1;
  if (!prevId.empty())
    for (int i = 0; i < n; i++)
      if (list_.alarms[i].id == prevId) { keep = i; break; }
  if (keep >= 0 && !list_.alarms[keep].acked) {
    selectedIdx_ = keep;                              // leave detail_ as the user left it
  } else {
    int fu = firstUnacked();
    if (fu >= 0) { selectedIdx_ = fu; detail_ = true; }   // jump to first unacked, auto-detail
    else         { selectedIdx_ = 0; detail_ = false; }   // all acked -> list top
  }
  clampSelection();
}

void AppCore::onNew(const NewPayload& n) { new_ = n; newPending_ = true; }

void AppCore::onHeartbeat(const Heartbeat& hb, bool stale) { hb_ = hb; stale_ = stale; }

void AppCore::nav(int delta) { selectedIdx_ += delta; clampSelection(); }

void AppCore::toggleDetail() {
  if (list_.valid && list_.count > 0) detail_ = !detail_;
}

void AppCore::toggleMute() { muted_ = !muted_; }

void AppCore::acknowledge() {
  int n = list_.valid ? (int)list_.alarms.size() : 0;
  if (n == 0 || selectedIdx_ < 0 || selectedIdx_ >= n) return;   // no focus -> no-op
  ackId_ = list_.alarms[selectedIdx_].id;
  ackPending_ = true;
  list_.alarms[selectedIdx_].acked = true;        // optimistic: don't wait for the republish
  // advance to the next unacked alarm, or drop to the list when none remain
  int fu = firstUnacked();
  if (fu >= 0) { selectedIdx_ = fu; detail_ = true; }
  else         { selectedIdx_ = 0; detail_ = false; }
  clampSelection();
}

bool AppCore::takeAckOne(std::string& id) {
  if (!ackPending_) return false;
  ackPending_ = false;
  id = ackId_;
  return true;
}

void AppCore::clampSelection() {
  int n = (list_.valid ? list_.count : 0);
  if (n <= 0) { selectedIdx_ = 0; return; }
  if (selectedIdx_ < 0) selectedIdx_ = 0;
  if (selectedIdx_ > n - 1) selectedIdx_ = n - 1;
}

std::string AppCore::detailText() const {
  if (selectedIdx_ < 0 || selectedIdx_ >= (int)list_.alarms.size()) return "";
  const Alarm& a = list_.alarms[selectedIdx_];
  return a.host + "\n" + a.name + "\n" + a.summary + "\n" + a.since;
}

RenderModel AppCore::render() {
  ViewState v = computeView(list_, newPending_, new_, hb_, stale_);
  newPending_ = false;   // one-shot beep consumed

  RenderModel m;
  m.led = v.led;
  m.beep = v.beep && !muted_;
  m.count = v.count;
  m.maxSeverity = v.maxSeverity;
  m.lines = v.lines;
  m.selectedIdx = selectedIdx_;

  if (v.conn != Conn::OK) {
    m.screen = Screen::STATUS;
    m.statusText = v.statusText;
  } else if (detail_ && v.count > 0) {
    m.screen = Screen::DETAIL;
    m.detailText = detailText();
  } else {
    m.screen = Screen::LIST;
  }
  return m;
}

} // namespace alarmcore
