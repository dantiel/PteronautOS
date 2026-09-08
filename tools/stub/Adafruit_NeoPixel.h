#pragma once
#include <stdint.h>
typedef uint16_t neoPixelType;
#define NEO_GRB 0x01
#define NEO_KHZ800 0x02
class Adafruit_NeoPixel {
public:
  Adafruit_NeoPixel(uint16_t n, int16_t pin, neoPixelType t) { (void)n; (void)pin; (void)t; }
  void begin() {}
  void setBrightness(uint8_t b) { (void)b; }
  void setPixelColor(uint16_t n, uint32_t c) { (void)n; (void)c; }
  void show() {}
};
