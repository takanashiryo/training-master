#pragma once
#include <algorithm>
#include <cmath>
#include <span>
namespace training {
// Audio-thread owned. Smooth game-thread gain changes over each stereo block.
class AudioEnvelope {
public:
    void reset(float gain=1.f) { gain_=gain; }
    void apply(std::span<float> stereo,float target) {
        if(!std::isfinite(target)) target=1.f;
        target=std::clamp(target,0.f,1.f);
        const auto frames=stereo.size()/2;
        if(!frames) return;
        if(gain_==1.f && target==1.f) return;
        const float start=gain_;
        for(std::size_t i=0;i<frames;++i) {
            const float gain=start+(target-start)*static_cast<float>(i+1)/static_cast<float>(frames);
            stereo[i*2]*=gain; stereo[i*2+1]*=gain;
        }
        gain_=target;
    }
private:
    float gain_{1.f};
};
}
