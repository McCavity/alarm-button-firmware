#include "appcore.h"

namespace alarmcore {

void AppCore::setList(const ListPayload& list) {
  list_ = list;
  if (!list_.valid || list_.count == 0) detail_ = false;
  clampSelection();
}

void AppCore::onNew(const NewPayload& n) { new_ = n; newPending_ = true; }

void AppCore::onHeartbeat(const Heartbeat& hb, bool stale) { hb_ = hb; stale_ = stale; }

void AppCore::nav(int delta) { selectedIdx_ += delta; clampSelection(); }

void AppCore::toggleDetail() {
  if (list_.valid && list_.count > 0) detail_ = !detail_;
}

void AppCore::toggleMute() { muted_ = !muted_; }

void AppCore::acknowledge() { ackPending_ = true; }

bool AppCore::takeAckRequest() {
  if (!ackPending_) return false;
  ackPending_ = false;
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
