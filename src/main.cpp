#include "sharing.h"
#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr uint8_t kSda = 4;
constexpr uint8_t kScl = 1;
constexpr uint8_t kDht = 3;
constexpr uint32_t kSampleInterval = 2500;
constexpr int kPanelWidth = 128;
constexpr int kPanelHeight = 64;
constexpr int kWidth = kPanelHeight;
constexpr int kHeight = kPanelWidth;

uint8_t oledAddress = 0;
uint8_t frame[kWidth * kHeight / 8];

struct Reading {
  int temperature;
  int humidity;
};

enum class SensorState { Waiting, Live, Error };
enum class DhtResult { Ok, Timeout, Checksum, Range };

constexpr char kCharacters[] = "0123456789.-%ACDEHIKLMNOPRSTUVW ";
constexpr uint8_t kGlyphs[][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x63, 0x13, 0x08, 0x64, 0x63}, // %
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x00, 0x00, 0x00, 0x00, 0x00}, // space
};
static_assert(sizeof(kGlyphs) / sizeof(kGlyphs[0]) == sizeof(kCharacters) - 1,
              "Font map and glyph table must match");

void pixel(int x, int y) {
  if (x >= 0 && x < kWidth && y >= 0 && y < kHeight) {
    // Map portrait coordinates back to the SSD1306's landscape memory.
    const int panelY = kPanelHeight - 1 - x;
    frame[y + (panelY / 8) * kPanelWidth] |= 1 << (panelY % 8);
  }
}

void text(int x, int y, const char* label, int scale = 1) {
  for (; *label; ++label, x += 6 * scale) {
    const char* match = strchr(kCharacters, *label);
    if (!match) continue;
    const auto& glyph = kGlyphs[match - kCharacters];
    for (int col = 0; col < 5; ++col) {
      for (int row = 0; row < 7; ++row) {
        if (!(glyph[col] & (1 << row))) continue;
        for (int dx = 0; dx < scale; ++dx) {
          for (int dy = 0; dy < scale; ++dy) {
            pixel(x + col * scale + dx, y + row * scale + dy);
          }
        }
      }
    }
  }
}

void centeredText(int y, const char* label, int scale = 1) {
  const int width = (6 * static_cast<int>(strlen(label)) - 1) * scale;
  text((kWidth - width) / 2, y, label, scale);
}

void formatReading(char (&out)[8], int tenths) {
  const int magnitude = abs(tenths);
  if (magnitude % 10 == 0) {
    snprintf(out, sizeof(out), "%d", tenths / 10);
  } else {
    snprintf(out, sizeof(out), "%s%d.%d", tenths < 0 ? "-" : "",
             magnitude / 10, magnitude % 10);
  }
}

void draw(SensorState state, const Reading& reading) {
  memset(frame, 0, sizeof(frame));
  const char* status = state == SensorState::Live ? "LIVE" :
                       state == SensorState::Waiting ? "WAIT" : "CHECK DHT";
  centeredText(7, status);
  for (int x = 3; x < kWidth - 3; ++x) {
    pixel(x, 19);
    pixel(x, 74);
  }
  centeredText(26, "TEMP");
  centeredText(82, "RH");
  text(33, 63, "C");
  pixel(27, 64);
  pixel(28, 63);
  pixel(29, 64);
  pixel(28, 65);
  centeredText(119, "%");

  char temperature[8] = "--";
  char humidity[8] = "--";
  if (state == SensorState::Live) {
    formatReading(temperature, reading.temperature);
    formatReading(humidity, reading.humidity);
  }
  const int temperatureScale = strlen(temperature) > 3 ? 2 : 3;
  const int humidityScale = strlen(humidity) > 3 ? 2 : 3;
  centeredText(39 + (21 - 7 * temperatureScale) / 2, temperature, temperatureScale);
  centeredText(94 + (21 - 7 * humidityScale) / 2, humidity, humidityScale);
}

bool sendOled(uint8_t control, const uint8_t* bytes, size_t count) {
  Wire.beginTransmission(oledAddress);
  Wire.write(control);
  Wire.write(bytes, count);
  return Wire.endTransmission() == 0;
}

bool initOled() {
  // SSD1306, 128 x 64, page addressing. Probe only the two OLED addresses.
  constexpr uint8_t setupCommands[] = {
      0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14,
      0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0x7F, 0xD9, 0xF1,
      0xDB, 0x40, 0xA4, 0xA6, 0x2E,
  };
  for (uint8_t candidate : {0x3C, 0x3D}) {
    oledAddress = candidate;
    if (sendOled(0x00, setupCommands, sizeof(setupCommands))) {
      Serial.printf("OLED connected at 0x%02X\n", oledAddress);
      return true;
    }
  }
  oledAddress = 0;
  Serial.println("OLED missing at 0x3C and 0x3D");
  return false;
}

bool flushOled() {
  for (int page = 0; page < kPanelHeight / 8; ++page) {
    const uint8_t pageCommands[] = {static_cast<uint8_t>(0xB0 + page), 0x00, 0x10};
    if (!sendOled(0x00, pageCommands, sizeof(pageCommands))) return false;
    for (int col = 0; col < kPanelWidth; col += 16) {
      if (!sendOled(0x40, frame + page * kPanelWidth + col, 16)) return false;
    }
  }
  return true;
}

void updateDisplay(SensorState state, const Reading& reading) {
  const bool needsInit = oledAddress == 0;
  if (needsInit && !initOled()) return;
  draw(state, reading);
  constexpr uint8_t displayOn = 0xAF;
  if (!flushOled() || (needsInit && !sendOled(0x00, &displayOn, 1))) {
    Serial.println("OLED write failed");
    oledAddress = 0;
  }
}

uint32_t pulseWidth(int level) {
  const uint32_t start = micros();
  while (digitalRead(kDht) == level) {
    if (micros() - start >= 120) return 0;
  }
  return micros() - start;
}

DhtResult readDht(Reading& reading) {
  uint8_t data[5] = {};
  // Pull low to request a sample, then release the line for the sensor.
  digitalWrite(kDht, LOW);
  pinMode(kDht, OUTPUT_OPEN_DRAIN);
  digitalWrite(kDht, LOW);
  delay(20);

  noInterrupts();
  pinMode(kDht, INPUT_PULLUP);
  // The initial high interval can be very short. Require both response pulses.
  pulseWidth(HIGH);
  const uint32_t responseLow = pulseWidth(LOW);
  const uint32_t responseHigh = pulseWidth(HIGH);
  bool complete = responseLow >= 35 && responseHigh >= 35;
  for (int bit = 0; bit < 40 && complete; ++bit) {
    const uint32_t low = pulseWidth(LOW);
    const uint32_t high = pulseWidth(HIGH);
    complete = low >= 15 && high >= 10;
    if (complete) {
      data[bit / 8] = (data[bit / 8] << 1) | (high > low);
    }
  }
  interrupts();

  if (!complete) return DhtResult::Timeout;
  if (static_cast<uint8_t>(data[0] + data[1] + data[2] + data[3]) != data[4]) {
    return DhtResult::Checksum;
  }
  const int humidity = data[0] * 10 + data[1];
  const int magnitude = (data[2] & 0x7F) * 10 + (data[3] & 0x7F);
  if (data[1] > 9 || (data[3] & 0x7F) > 9 || humidity > 1000 || magnitude > 999) {
    return DhtResult::Range;
  }
  reading = {(data[3] & 0x80) ? -magnitude : magnitude, humidity};
  return DhtResult::Ok;
}

const char* dhtError(DhtResult result) {
  switch (result) {
    case DhtResult::Timeout: return "pulse timeout";
    case DhtResult::Checksum: return "checksum";
    case DhtResult::Range: return "invalid reading";
    case DhtResult::Ok: return "ok";
  }
  return "unknown";
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("THERMO LAN PORTRAIT  SDA=4 SCL=1 DHT=3");
  pinMode(kDht, INPUT_PULLUP);
  Wire.begin(kSda, kScl, 100000);
  Wire.setTimeOut(30);
  delay(100);
  updateDisplay(SensorState::Waiting, {});
  startSharing();
  delay(1400);
}

void loop() {
  const uint32_t started = millis();
  Reading reading{};
  const DhtResult result = readDht(reading);
  updateDisplay(result == DhtResult::Ok ? SensorState::Live : SensorState::Error, reading);
  if (result == DhtResult::Ok) {
    queueSample(reading.temperature / 10.0f, reading.humidity / 10.0f);
    Serial.printf("TEMP=%.1f C  RH=%.1f %%  OLED=0x%02X\n",
                  reading.temperature / 10.0, reading.humidity / 10.0, oledAddress);
  } else {
    markSampleFailed();
    Serial.printf("DHT11 error: %s\n", dhtError(result));
  }
  const uint32_t elapsed = millis() - started;
  if (elapsed < kSampleInterval) delay(kSampleInterval - elapsed);
}

