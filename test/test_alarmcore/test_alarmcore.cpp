#include <unity.h>
#include "parser.h"
#include "view.h"
#include "debounce.h"
#include "quadrature.h"
#include "pressclassifier.h"

using namespace alarmcore;

static const char* LIST_JSON =
  R"({"schema_version":1,"device_id":"office","count":2,"max_severity":"critical","alarms":[)"
  R"({"id":"a1","host":"host01","name":"USV auf Batterie","severity":"critical","summary":"x","since":"2026-06-27T10:00:00.000+02:00"},)"
  R"({"id":"b2","host":"host02","name":"Disk","severity":"warning","summary":"y","since":"2026-06-27T09:00:00.000+02:00"}]})";

void test_parseList_ok() {
  ListPayload p = parseList(LIST_JSON);
  TEST_ASSERT_TRUE(p.valid);
  TEST_ASSERT_EQUAL_INT(2, p.count);
  TEST_ASSERT_EQUAL_STRING("office", p.device_id.c_str());
  TEST_ASSERT_EQUAL_STRING("critical", p.max_severity.c_str());
  TEST_ASSERT_EQUAL_INT(2, (int)p.alarms.size());
  TEST_ASSERT_EQUAL_STRING("host01", p.alarms[0].host.c_str());
  // Offset durchgereicht, nicht auf Z normalisiert
  TEST_ASSERT_EQUAL_STRING("2026-06-27T10:00:00.000+02:00", p.alarms[0].since.c_str());
}

void test_parseList_empty() {
  ListPayload p = parseList(R"({"schema_version":1,"device_id":"office","count":0,"max_severity":null,"alarms":[]})");
  TEST_ASSERT_TRUE(p.valid);
  TEST_ASSERT_EQUAL_INT(0, p.count);
  TEST_ASSERT_EQUAL_INT(0, (int)p.alarms.size());
}

void test_parseList_malformed() {
  ListPayload p = parseList("nicht-json{");
  TEST_ASSERT_FALSE(p.valid);
}

void test_parseList_severity_failsafe() {
  ListPayload p = parseList(R"({"count":1,"alarms":[{"id":"x","host":"h"}]})");
  TEST_ASSERT_TRUE(p.valid);
  TEST_ASSERT_EQUAL_STRING("warning", p.alarms[0].severity.c_str());
}

void test_parseList_host_failsafe() {
  ListPayload p = parseList(R"({"count":1,"alarms":[{"id":"x","severity":"warning"}]})");
  TEST_ASSERT_EQUAL_STRING("unknown", p.alarms[0].host.c_str());
}

void test_parseHeartbeat_ok() {
  Heartbeat h = parseHeartbeat(R"({"schema_version":1,"grafana_ok":true,"poll_age_s":8})");
  TEST_ASSERT_TRUE(h.valid);
  TEST_ASSERT_TRUE(h.grafana_ok);
  TEST_ASSERT_EQUAL_INT(8, h.poll_age_s);
}

void test_parseHeartbeat_age_null() {
  Heartbeat h = parseHeartbeat(R"({"grafana_ok":false,"poll_age_s":null})");
  TEST_ASSERT_TRUE(h.valid);
  TEST_ASSERT_FALSE(h.grafana_ok);
  TEST_ASSERT_EQUAL_INT(-1, h.poll_age_s);
}

void test_parseNew_ok() {
  NewPayload n = parseNew(R"({"schema_version":1,"count_new":1,"max_severity":"warning"})");
  TEST_ASSERT_TRUE(n.valid);
  TEST_ASSERT_EQUAL_INT(1, n.count_new);
  TEST_ASSERT_EQUAL_STRING("warning", n.max_severity.c_str());
}

void test_view_alarms_blink_ok() {
  ListPayload p = parseList(LIST_JSON);
  Heartbeat h; h.valid = true; h.grafana_ok = true; h.poll_age_s = 2;
  ViewState v = computeView(p, false, NewPayload{}, h, false);
  TEST_ASSERT_EQUAL_INT((int)LedMode::BLINK_FAST, (int)v.led);
  TEST_ASSERT_FALSE(v.beep);
  TEST_ASSERT_EQUAL_INT(2, (int)v.lines.size());
  TEST_ASSERT_EQUAL_STRING("host01 USV auf Batterie", v.lines[0].c_str());
  TEST_ASSERT_EQUAL_STRING("OK", v.statusText.c_str());
}

void test_view_empty_led_off() {
  ListPayload p = parseList(R"({"count":0,"alarms":[]})");
  ViewState v = computeView(p, false, NewPayload{}, Heartbeat{}, false);
  TEST_ASSERT_EQUAL_INT((int)LedMode::OFF, (int)v.led);
}

void test_view_new_triggers_beep() {
  ListPayload p = parseList(LIST_JSON);
  NewPayload n = parseNew(R"({"count_new":1,"max_severity":"warning"})");
  Heartbeat h; h.valid = true; h.grafana_ok = true; h.poll_age_s = 2;
  ViewState v = computeView(p, true, n, h, false);
  TEST_ASSERT_TRUE(v.beep);
}

void test_view_stale_iobroker_down() {
  ViewState v = computeView(ListPayload{}, false, NewPayload{}, Heartbeat{}, true);
  TEST_ASSERT_EQUAL_INT((int)Conn::IOBROKER_DOWN, (int)v.conn);
  TEST_ASSERT_EQUAL_STRING("ioBroker?", v.statusText.c_str());
}

void test_view_grafana_down() {
  ListPayload p = parseList(LIST_JSON);
  Heartbeat h; h.valid = true; h.grafana_ok = false; h.poll_age_s = 5;
  ViewState v = computeView(p, false, NewPayload{}, h, false);
  TEST_ASSERT_EQUAL_INT((int)Conn::GRAFANA_DOWN, (int)v.conn);
  TEST_ASSERT_EQUAL_STRING("Grafana?", v.statusText.c_str());
}

void test_debounce_stabilizes_after_window() {
  Debouncer d(5);
  TEST_ASSERT_FALSE(d.update(true, 0));    // raw high, but not yet stable
  TEST_ASSERT_FALSE(d.update(true, 4));    // 4 ms < 5 ms window
  TEST_ASSERT_TRUE(d.update(true, 5));     // 5 ms reached -> flips
  TEST_ASSERT_TRUE(d.state());
}

void test_debounce_rejects_bounce() {
  Debouncer d(5);
  d.update(true, 0);
  TEST_ASSERT_FALSE(d.update(false, 2));   // bounced back before window
  TEST_ASSERT_FALSE(d.update(false, 6));   // stable low == initial, no flip
  TEST_ASSERT_FALSE(d.state());
}

void test_quadrature_cw_one_detent() {
  QuadratureDecoder q(4);
  TEST_ASSERT_EQUAL_INT(0, q.update(true, false));   // 00 -> 10
  TEST_ASSERT_EQUAL_INT(0, q.update(true, true));    // 10 -> 11
  TEST_ASSERT_EQUAL_INT(0, q.update(false, true));   // 11 -> 01
  TEST_ASSERT_EQUAL_INT(1, q.update(false, false));  // 01 -> 00  (detent complete)
}

void test_quadrature_ccw_one_detent() {
  QuadratureDecoder q(4);
  TEST_ASSERT_EQUAL_INT(0, q.update(false, true));   // 00 -> 01
  TEST_ASSERT_EQUAL_INT(0, q.update(true, true));    // 01 -> 11
  TEST_ASSERT_EQUAL_INT(0, q.update(true, false));   // 11 -> 10
  TEST_ASSERT_EQUAL_INT(-1, q.update(false, false)); // 10 -> 00  (detent complete)
}

void test_press_short_click_on_release() {
  PressClassifier p(1500);
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(true, 0));    // press
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(true, 100));  // held briefly
  TEST_ASSERT_EQUAL_INT((int)PressEvent::SHORT_CLICK, (int)p.update(false, 120)); // release
}

void test_press_long_press_fires_once() {
  PressClassifier p(1500);
  p.update(true, 0);                                                       // press
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(true, 1499));
  TEST_ASSERT_EQUAL_INT((int)PressEvent::LONG_PRESS, (int)p.update(true, 1500));
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(true, 1800)); // still held
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(false, 1900));// release: no short
}

void test_press_idle_is_none() {
  PressClassifier p(1500);
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(false, 0));
  TEST_ASSERT_EQUAL_INT((int)PressEvent::NONE, (int)p.update(false, 5000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_debounce_stabilizes_after_window);
  RUN_TEST(test_debounce_rejects_bounce);
  RUN_TEST(test_quadrature_cw_one_detent);
  RUN_TEST(test_quadrature_ccw_one_detent);
  RUN_TEST(test_press_short_click_on_release);
  RUN_TEST(test_press_long_press_fires_once);
  RUN_TEST(test_press_idle_is_none);
  RUN_TEST(test_parseList_ok);
  RUN_TEST(test_parseList_empty);
  RUN_TEST(test_parseList_malformed);
  RUN_TEST(test_parseList_severity_failsafe);
  RUN_TEST(test_parseList_host_failsafe);
  RUN_TEST(test_parseHeartbeat_ok);
  RUN_TEST(test_parseHeartbeat_age_null);
  RUN_TEST(test_parseNew_ok);
  RUN_TEST(test_view_alarms_blink_ok);
  RUN_TEST(test_view_empty_led_off);
  RUN_TEST(test_view_new_triggers_beep);
  RUN_TEST(test_view_stale_iobroker_down);
  RUN_TEST(test_view_grafana_down);
  return UNITY_END();
}
