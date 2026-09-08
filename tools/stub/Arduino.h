#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define HIGH 0x1
#define LOW  0x0
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define SERIAL_8N1 0x800001cUL
void pinMode(int pin, int mode);
int  digitalRead(int pin);
void digitalWrite(int pin, int val);
void delay(unsigned long ms);
unsigned long millis(void);
unsigned long micros(void);
template<class T, class U, class V> T constrain(T amt, U low, V high) { return (amt < (T)low) ? (T)low : ((amt > (T)high) ? (T)high : amt); }
template<class T> T min(T a, T b) { return a < b ? a : b; }
template<class T> T max(T a, T b) { return a > b ? a : b; }
class SerialType {
public:
  void begin(unsigned long baud) { (void)baud; }
  void begin(unsigned long baud, uint32_t cfg, int rx, int tx) { (void)baud; (void)cfg; (void)rx; (void)tx; }
  void end() {}
  bool setTX(int pin) { (void)pin; return true; }
  bool setRX(int pin) { (void)pin; return true; }
  int available() { return 0; }
  int availableForWrite() { return 1; }
  int read() { return 0; }
  size_t write(uint8_t b) { (void)b; return 1; }
  size_t write(int b) { (void)b; return 1; }
  size_t write(const uint8_t* b, size_t n) { (void)b; return n; }
  size_t write(const char *s) { (void)s; return 1; }
  bool overflow() { return false; }
  void print(const char *s) { (void)s; }
  void print(char c) { (void)c; }
  void print(int v) { (void)v; }
  void print(uint32_t v) { (void)v; }
  void print(unsigned long v) { (void)v; }
  void println() {}
  void println(const char *s) { (void)s; }
  void println(int v) { (void)v; }
};
extern SerialType Serial;
extern SerialType Serial1;
extern SerialType Serial2;
