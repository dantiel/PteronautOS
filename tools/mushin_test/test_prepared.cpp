#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>
#include "PreparedMotion.h"

int main() {
    unsigned checks=0;
    for(float ld : {0.f, 1.f, 7.f, 7.99f, 8.f})
    for(float lu : {0.f, 1.f, 7.f, 7.99f, 8.f})
    for(float mix : {0.f, 25.f, 100.f})
    for(float skew : {-100.f, 0.f, 100.f}) {
        float wd=std::fmax(8-ld,0.01f), wu=std::fmax(8-lu,0.01f);
        float boundary=Motion::twoPi*wd/(wd+wu);
        Motion::Recipe r;
        Motion::prepare(r,boundary,ld,lu,lu,ld,mix,skew,-skew);
        assert(Motion::valid(r));
        for(int j=0;j<1024;++j) {
            float phase=j/1024.f, l, rr;
            Motion::waves(r,phase,l,rr);
            float a=FlappingOscillator::shapeWave(phase*Motion::twoPi,ld,lu,boundary,mix,skew,-skew);
            float b=FlappingOscillator::shapeWave(phase*Motion::twoPi,lu,ld,boundary,mix,skew,-skew);
            assert(std::fabs(l-a)<0.001f); assert(std::fabs(rr-b)<0.001f);
            checks+=2;
        }
        uint8_t bytes[Motion::wireSize+4]={};
        Motion::encode(r,bytes);
        Motion::Recipe restored;
        assert(Motion::decode(bytes,restored));
        for(unsigned i=Motion::wireSize;i<sizeof(bytes);++i) assert(bytes[i]==0);
        Motion::Receiver rx;
        Motion::Recipe active; active.hz=3;
        for(unsigned i=0;i<Motion::chunks;++i) {
            uint8_t p[59]={42,0,(uint8_t)i};
            size_t offset=i*Motion::chunkSize;
            size_t n=Motion::wireSize-offset<Motion::chunkSize?Motion::wireSize-offset:Motion::chunkSize;
            memcpy(p+3,bytes+offset,n);
            bool accepted=rx.accept(p,n+3,10+i,active);
            assert(accepted==(i==Motion::chunks-1));
            if(!accepted) assert(active.hz==3);
        }
        assert(active.hz==r.hz);
    }
    Motion::Recipe r;
    r.kind[0]=1; r.kind[1]=2; r.kind[2]=3; r.kind[3]=4;
    r.centre[0]=95; r.centre[1]=105; r.trim[0]=12; r.trim[1]=-12;
    r.backTrim[0]=20; r.backTrim[1]=-20;
    r.minimum=600; r.maximum=2400;
    uint16_t out[7]; Motion::outputs(r,0,out);
    assert(out[0]==1562 && out[1]==1638 && out[2]==1582 && out[3]==1618);
    uint8_t bytes[Motion::wireSize];
    r.hz=std::numeric_limits<float>::quiet_NaN(); Motion::encode(r,bytes);
    Motion::Recipe dest; assert(!Motion::decode(bytes,dest));
    r.hz=4; r.kind[0]=255; assert(!Motion::valid(r));
    printf("%u waveform parity comparisons passed; codec, atomic transfer, calibration and invalid-value checks passed\n",checks);
}
