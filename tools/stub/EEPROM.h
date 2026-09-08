#pragma once
#include <stdint.h>
class EEPROMClass {
public:
  void begin(size_t size) { (void)size; }
  uint8_t read(int addr) { (void)addr; return 0; }
  void write(int addr, uint8_t v) { (void)addr; (void)v; }
  void commit() {}
};
extern EEPROMClass EEPROM;
