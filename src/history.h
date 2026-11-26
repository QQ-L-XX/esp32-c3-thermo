#pragma once
#include <stdint.h>
#include <stddef.h>

// One successful sample per five-minute slot. A delayed first sample can leave
// both endpoints of 289 different slots inside a rolling 24-hour window.
struct HistoryPoint {
  int64_t seconds;
  float temperature;
  float humidity;
};

class History {
public:
  static constexpr int interval = 300;
  static constexpr int window = 86400;
  static constexpr size_t capacity = window / interval + 1;

  void record(int64_t seconds, float temperature, float humidity) {
    if (count && points[(next + capacity - 1) % capacity].seconds / interval == seconds / interval) return;
    points[next] = {seconds, temperature, humidity};
    next = (next + 1) % capacity;
    if (count < capacity) ++count;
  }

  size_t snapshot(int64_t now, HistoryPoint* output) const {
    size_t copied = 0;
    const size_t first = (next + capacity - count) % capacity;
    for (size_t i = 0; i < count; ++i) {
      const auto& point = points[(first + i) % capacity];
      if (now - point.seconds < window) output[copied++] = point;
    }
    return copied;
  }
private:
  HistoryPoint points[capacity]{};
  size_t next = 0, count = 0;
};
