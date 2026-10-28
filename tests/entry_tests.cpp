#include "core/session.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace training;
namespace {
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
struct Game final:Backend {
    float position{}; bool paused{},muted{},finished{},fail_chart{};
    std::vector<float> chart_seeks,audio_seeks;
    std::vector<char> operations;
    bool pause(bool value) override { paused=value; operations.push_back(value?'P':'p'); return true; }
    bool seek(float value) override { position=value; audio_seeks.push_back(value); operations.push_back('S'); return true; }
    bool seek_chart(float value) override {
        check(muted && paused,"entry must be silent and natively paused before moving chart");
        if(fail_chart) return false;
        position=value; chart_seeks.push_back(value); operations.push_back('C'); return true;
    }
    bool mute(bool value) override { muted=value; operations.push_back(value?'M':'m'); return true; }
    bool rate(float) override { return true; }
    bool finish() override { finished=true; return true; }
};
void command(Session& s,Action action,float value=0) { s.submit({s.snapshot().generation,action,value}); }
void step(Session& s,Game& g,float dt=.025f) { s.tick(g.position,g.paused,g,dt); }
void begin(Session& s,Game& g,float position=90.f) {
    Track t{"test.vox",1,3,180.f}; t.autoplay_ready=t.camera_ready=true;
    s.begin(t,true); g.position=position; step(s,g,0);
}
void finish_entry(Session& s,Game& g) {
    for(unsigned n=0;n<100 && s.snapshot().rewinding;++n) step(s,g);
    check(!s.snapshot().rewinding,"entry must finish within its bounded duration");
}
}
int main() {
    try {
        Session s; Game g; begin(s,g);
        command(s,Action::Enable); step(s,g);
        check(s.suppress_result() && s.snapshot().enabled && s.snapshot().rewinding,
              "enable must latch protection and enter animated rewind");
        check(g.operations[0]=='M' && g.operations[1]=='P',"mute must precede native pause and any chart rewind");
        check(s.snapshot().position>0 && s.snapshot().position<90 && g.audio_seeks.empty(),
              "first frame must visibly move backwards without teleporting or seeking audio");
        command(s,Action::SetIn,20); command(s,Action::SetOut,40); command(s,Action::SetLoop,1);
        command(s,Action::SetRepeat,1); // Repeat must never auto-resume a training-entry rewind.
        command(s,Action::Enable); // Repeated enable must not restart the animation.
        finish_entry(s,g);
        check(g.chart_seeks.size()>10,"rewind must expose multiple visible intermediate frames");
        for(std::size_t i=1;i<g.chart_seeks.size();++i)
            check(g.chart_seeks[i]<g.chart_seeks[i-1],"entry chart positions must decrease monotonically");
        float previous_step=90.f-g.chart_seeks.front();
        for(std::size_t i=1;i<g.chart_seeks.size();++i) {
            const float distance=g.chart_seeks[i-1]-g.chart_seeks[i];
            check(distance<previous_step,"equal-duration steps must slow down continuously toward the start");
            previous_step=distance;
        }
        check(g.chart_seeks[g.chart_seeks.size()/2]<90.f*.18f,
              "most rewind distance must finish early, leaving a visible ease-out tail");
        check(s.snapshot().position==0 && s.snapshot().paused && g.position==0 && g.paused && g.muted,
              "entry must stop at absolute song start and remain silent/paused");
        check(g.audio_seeks==std::vector<float>{0.f} && s.snapshot().loops==0,
              "audio aligns once at zero; loop selection must not interrupt entry");
        const auto count=g.chart_seeks.size(); step(s,g);
        check(g.chart_seeks.size()==count && g.muted,"waiting at zero must not rewind again or unmute");
        command(s,Action::Resume); step(s,g);
        check(!g.paused && !g.muted && !s.snapshot().paused && s.suppress_result(),"Play releases entry mute while retaining protection");
        for(Action action:{Action::SetAutoplay,Action::SetCameraLocked}) {
            Game next; s.end(g); begin(s,next,100);
            command(s,action,1); step(s,next);
            check(s.snapshot().rewinding && next.muted,"implicit training enable must use the same silent entry");
            finish_entry(s,next); s.end(next);
            check(!next.muted && !s.snapshot().rewinding,"native exit must release the music-bus override");
        }
        for(Action action:{Action::Disable,Action::Finish}) {
            Game next; begin(s,next,60); command(s,Action::Enable); step(s,next);
            command(s,action); step(s,next);
            check(!s.snapshot().rewinding && !next.muted && s.suppress_result(),"cancel/finish must clean up mute and retain taint");
        }
        Game broken; begin(s,broken,50); broken.fail_chart=true;
        command(s,Action::Enable); step(s,broken);
        check(!s.snapshot().ready && s.snapshot().paused && broken.muted && s.suppress_result(),
              "chart failure must stop silently and retain score guard");
        command(s,Action::Finish); step(s,broken); check(!broken.muted && broken.finished,"Finish must recover from failed entry");
        Game zero; begin(s,zero,0); command(s,Action::Enable); step(s,zero);
        check(!s.snapshot().rewinding && zero.paused && zero.muted && zero.chart_seeks.empty(),
              "already at zero must pause silently without inventing reverse motion");
        s.end(zero);
        std::cout<<"Visible silent training entry, explicit/implicit enable, mute/seek ordering and cleanup passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
