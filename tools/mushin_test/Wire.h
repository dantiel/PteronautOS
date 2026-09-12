#pragma once
#include <stdint.h>
#include <stddef.h>
class TwoWire {
public:
  static int16_t rate;      // gyro Z rate to return (for gyroZRate)
  static uint8_t whoami;    // MPU whoami (for gyroInit)
  int read_phase = 0;
  void begin() {}
  void begin(int, int, uint32_t) {}
  bool setSDA(int) { return true; }
  bool setSCL(int) { return true; }
  void beginTransmission(uint8_t) {}
  void beginTransmission(int) {}
  size_t write(uint8_t) { return 1; }
  uint8_t endTransmission() { return 0; }
  uint8_t endTransmission(bool) { return 0; }
  size_t requestFrom(uint8_t, size_t n) { read_phase = 0; return n; }
  size_t requestFrom(int, int n) { read_phase = 0; return (size_t)n; }
  int available() { return 2; }
  int read() {
    if (read_phase == 0) { read_phase = 1; return (int)(((uint16_t)rate >> 8) & 0xFF); }
    return (int)((uint16_t)rate & 0xFF);
  }
};
extern TwoWire Wire;
