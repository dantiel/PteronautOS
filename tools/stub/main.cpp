#include "Arduino.h"
SerialType Serial, Serial1, Serial2;
void pinMode(int, int) {}
int  digitalRead(int) { return HIGH; }
void digitalWrite(int, int) {}
void delay(unsigned long) {}
unsigned long millis() { return 0; }
unsigned long micros() { return 0; }
