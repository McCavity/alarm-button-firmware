#pragma once

// Pure scroll-window arithmetic for the alarm list (Phase 2, strand 2). Host-testable.
namespace alarmcore {

constexpr int LIST_ROWS = 7;   // 160x80 display, header 16 px, row 9 px -> 7 rows

// First visible index so that `focus` is visible; keeps prevTop when possible (no jitter).
inline int scrollTop(int focus, int n, int rows, int prevTop) {
  if (n <= rows || rows <= 0) return 0;
  int top = prevTop;
  if (focus < top) top = focus;
  if (focus >= top + rows) top = focus - rows + 1;
  if (top < 0) top = 0;
  if (top > n - rows) top = n - rows;
  return top;
}

} // namespace alarmcore
