#include <cassert>
#include <cmath>
#include <cstdio>
#include "Ornithopter.h"
uint32_t ChannelData[16];
static uint32_t clockUs=1000;
unsigned long micros() { return clockUs; }
int main() {
    for(auto& c:ChannelData) c=992;
    ChannelData[4]=1811; ChannelData[2]=172;
    ornithopter.linkUp=true; ornithopter.enabled=true;
    ornithopter.update(); // arm only with zero throttle
    ornithopter.ferocityShapeMix=100;
    ornithopter.strokeFerocity=90; ornithopter.returnFerocity=10;
    ornithopter.strokeSkew=80; ornithopter.returnSkew=-60;
    ornithopter.aileronScale=0; ornithopter.elevatorScale=0;
    ornithopter.aileronSkewMix=100;
    ornithopter.servoSpeed=42;
    ornithopter.flapBaseFreq=200;
    ornithopter.throttleFrequencyMix=100;
    ornithopter.servoMinUs=600; ornithopter.servoMaxUs=2400;
    ChannelData[0]=1811; ChannelData[2]=1811;
    clockUs+=3000; ornithopter.update();
    const auto& r=ornithopter.motionRecipe;
    assert(Motion::valid(r) && r.flapping && r.hz==20);
    assert(r.mix==1 && r.skew[0]>0 && r.skew[1]<0);
    assert(r.boundary<0.5f);
    assert(std::fabs(r.amplitude[0])>std::fabs(r.amplitude[1]));
    assert(r.kind[0]==1 && r.kind[1]==2 && r.kind[2]==5);
    assert(r.minimum==600 && r.maximum==2400);
    // Glide is an explicit prepared position, not a throttled full-amplitude wave.
    ChannelData[2]=172; clockUs+=3000; ornithopter.update();
    assert(!r.flapping && r.hz==0 && r.amplitude[0]==0 && r.amplitude[1]==0);
    uint16_t outputs[7]; Motion::outputs(r,0.73f,outputs);
    assert(outputs[0]==ornithopter.funcValue(SF_LEFT_WING));
    assert(outputs[1]==ornithopter.funcValue(SF_RIGHT_WING));
    setOrnithopterProfile(SERVO_4WING); clockUs+=3000; ornithopter.update();
    assert(r.kind[2]==3 && r.kind[3]==4);
    // Gearbox profiles use their existing EP2 mixer, no oscillator on either chip.
    setOrnithopterProfile(GEARBOX_1MOT_2VTAIL);
    clockUs+=3000; ornithopter.update();
    assert(r.kind[0]==6 && r.kind[1]==0 && !r.flapping);
    Motion::outputs(r,0,outputs);
    assert(outputs[0]==ornithopter.funcValue(SF_MOTOR));
    assert(outputs[1]==ornithopter.funcValue(SF_VTAIL_LEFT));
    puts("EP2 preparer: asymmetric profile, full coupling, differential amplitude, calibration, glide and gearbox passed");
}
