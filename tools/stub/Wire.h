#pragma once
#include <stdint.h>
#include <stddef.h>
class TwoWire {
public:
  void begin() {}
  void begin(int sda, int scl, uint32_t freq) { (void)sda; (void)scl; (void)freq; }
  bool setSDA(int pin) { (void)pin; return true; }
  bool setSCL(int pin) { (void)pin; return true; }
  void beginTransmission(uint8_t addr) { (void)addr; }
  void beginTransmission(int addr) { (void)addr; }
  size_t write(uint8_t v) { (void)v; return 1; }
  uint8_t endTransmission() { return 0; }
  uint8_t endTransmission(bool stop) { (void)stop; return 0; }
  size_t requestFrom(uint8_t addr, size_t n) { (void)addr; (void)n; return 0; }
  size_t requestFrom(int addr, int n) { (void)addr; (void)n; return 0; }
  int available() { return 0; }
  int read() { return 0; }
};
extern TwoWire Wire;
