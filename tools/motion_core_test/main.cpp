#include <cassert>
#include <cstdio>
#include "Yoshimitsu.h"
static uint32_t nowMs=1000,nowUs=1000000;
uint8_t SerialType::tx[3][1024];
int16_t SerialType::txlen[3]={};
int16_t TwoWire::rate=0;
uint8_t TwoWire::whoami=0x68;
SerialType Serial,Serial1,Serial2;
TwoWire Wire;
EEPROMClass EEPROM;
unsigned long millis() { return nowMs; }
unsigned long micros() { return nowUs; }
void delay(unsigned long) {}
void pinMode(int,int) {}
int digitalRead(int) { return HIGH; }
void digitalWrite(int,int) {}
static void tick(unsigned ms) { nowMs+=ms; nowUs+=ms*1000; loop1(); }
int main() {
    static_assert(YOSHI_MOTION_CORE==1,"Test the real worker, not cooperative fallback");
    servoCount=3;
    servos[0].attach(2,500,2500); servos[1].attach(3,500,2500); servos[2].attach(4,500,2500);
    assert(top[1]+1==1000000/YOSHI_SERVO_HZ);
    motionRecipe.kind[0]=1; motionRecipe.kind[1]=2; motionRecipe.kind[2]=6;
    motionRecipe.output[2]=1700;
    motionRecipe.centre[0]=motionRecipe.centre[1]=90;
    motionRecipe.amplitude[0]=-60; motionRecipe.amplitude[1]=60;
    motionRecipe.hz=10; motionRecipe.flapping=1;
    Motion::prepare(motionRecipe,Motion::twoPi/2,0,0,0,0,100,0,0);
    motionSetupReady.store(true);
    motionPublish(nowMs); loop1();
    const unsigned start=pulse[2];
    assert(start<1500 && pulse[3]>1500 && pulse[4]==1700);
    // No UART, gyro or main-loop calls at all: the independent clock runs.
    tick(25); assert(pulse[2]>=1490 && pulse[2]<=1510);
    tick(25); assert(pulse[2]>1500 && pulse[3]<1500);
    // New recipe must not reset phase; only frequency/shape change.
    motionPublish(nowMs); tick(1); assert(pulse[2]>1500);
    // A pending snapshot is immutable even if main computes a newer recipe.
    motionRecipe.hz=12; motionPublish(nowMs);
    motionRecipe.hz=18; motionPublish(nowMs);
    assert(motionPending.hz==12); tick(1);
    // Main stalls: worker must enforce its own freshness deadline, including motor idle.
    tick(101); assert(pulse[2]==1500 && pulse[3]==1500 && pulse[4]==1000);
    motionPublish(nowMs); tick(1); assert(pulse[4]==1700);
    motionCancel(); tick(1); assert(pulse[2]==1500 && pulse[4]==1000);
    // A cancelled queued command cannot revive motion later.
    motionPublish(nowMs); motionCancel(); tick(1); assert(pulse[4]==1000);
    puts("Core-1 phase continuity, mailbox ownership, cancellation, autonomous failsafe and PWM rate passed");
}
