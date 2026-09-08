#pragma once
#include <stdint.h>
class Servo {
public:
  bool attach(int pin, int minUs, int maxUs) { (void)pin; (void)minUs; (void)maxUs; return true; }
  void detach() {}
  bool attached() { return false; }
  void writeMicroseconds(int us) { (void)us; }
};
