#pragma once
#include <stdint.h>
class Servo {
public:
  int  last_us = 1500;
  int  write_count = 0;
  bool attach(int pin, int minUs, int maxUs) { (void)pin; (void)minUs; (void)maxUs; return true; }
  void detach() {}
  bool attached() { return false; }
  void writeMicroseconds(int us) { last_us = us; write_count++; }
};
