#include "core/session.hpp"
#include "core/audio_envelope.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <vector>
using namespace training;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
struct Game final:Backend {
    float position{},speed{1.f}; bool paused{},muted{},reversing{},fail_chart{},fail_seek{},fail_resume{};
    std::vector<float> chart_seeks,audio_seeks;
    std::vector<char> operations;
    bool pause(bool value) override {
        if(!value && fail_resume) return false;
        paused=value; operations.push_back(value?'P':'p'); return true;
    }
    bool seek(float value) override {
        if(fail_seek) return false;
        position=value; audio_seeks.push_back(value); operations.push_back('S'); return true;
    }
    bool seek_chart(float value) override {
        check(paused && muted && !reversing,"return motion must be silent with native clock and reverse PCM stopped");
        if(fail_chart) return false;
        position=value; chart_seeks.push_back(value); operations.push_back('C'); return true;
    }
    bool mute(bool value) override { muted=value; operations.push_back(value?'M':'m'); return true; }
    bool rate(float value) override { speed=value; return true; }
    bool reverse(bool value) override { reversing=value; return true; }
    float gain{1};
    void music_gain(float value) override { gain=value; }
    bool finish() override { return true; }
};
void command(Session& s,Action a,float value=0) { s.submit({s.snapshot().generation,a,value}); }
void step(Session& s,Game& g,float dt=.025f) { s.tick(g.position,g.paused,g,dt); }
void finish_return(Session& s,Game& g) {
    for(unsigned i=0;i<100 && s.snapshot().rewinding;++i) step(s,g);
    check(!s.snapshot().rewinding,"return must terminate in bounded time");
}
void begin(Session& s,Game& g) {
    Track track{"loop.vox",1,3,180.f};
    auto clip=std::make_shared<PcmClip>(); clip->samples.resize(2,0);
    track.reverse_audio=std::make_shared<ReverseAsset>(clip);
    g=Game{}; s.set_loop_buffers(0,0); s.begin(track,true); command(s,Action::Enable); step(s,g);
    command(s,Action::SetIn,20); command(s,Action::SetOut,40); command(s,Action::SetLoop,1);
    command(s,Action::SetRate,.5f); command(s,Action::Resume); step(s,g);
    g.chart_seeks.clear(); g.audio_seeks.clear(); g.operations.clear();
}
void cross_out(Session& s,Game& g) { g.position=40.01f; step(s,g); }
}
int main() {
    try {
        {
            Session cycle; Game playback; begin(cycle,playback);
            command(cycle,Action::CycleLoop); step(cycle,playback);
            check(!cycle.snapshot().loop && !cycle.snapshot().repeat,"once must cycle to off");
            command(cycle,Action::CycleLoop); step(cycle,playback);
            check(cycle.snapshot().loop && cycle.snapshot().repeat,"off must cycle to repeat atomically");
            cross_out(cycle,playback); finish_return(cycle,playback);
            check(cycle.snapshot().position==20 && !cycle.snapshot().paused && cycle.snapshot().loops==1,
                  "repeat icon mode must rewind silently to IN and resume");
            command(cycle,Action::CycleLoop); step(cycle,playback);
            check(cycle.snapshot().loop && !cycle.snapshot().repeat,"repeat must cycle to once");
            cross_out(cycle,playback); finish_return(cycle,playback);
            check(cycle.snapshot().position==20 && cycle.snapshot().paused && cycle.snapshot().loops==2,
                  "once icon mode must rewind to IN and pause");
            command(cycle,Action::CycleLoop); step(cycle,playback);
            check(!cycle.snapshot().loop && !cycle.snapshot().repeat,"once must cycle back to off");
            command(cycle,Action::CycleLoop); command(cycle,Action::CycleLoop); step(cycle,playback);
            check(cycle.snapshot().loop && !cycle.snapshot().repeat,"two queued clicks from off must reach once");
            command(cycle,Action::CycleLoop); command(cycle,Action::CycleLoop); command(cycle,Action::CycleLoop); step(cycle,playback);
            check(cycle.snapshot().loop && !cycle.snapshot().repeat,"three queued clicks must complete an entire cycle");
        }
        Session s; Game g; begin(s,g);
        check(!s.snapshot().repeat,"Repeat must default off");
        check(s.snapshot().position==20 && g.position==20 && !s.snapshot().rewinding && !g.paused,
              "Loop playback beginning before IN must immediately seek to IN, not play from zero");
        {
            Session entry; Game playback; begin(entry,playback); entry.set_loop_buffers(3,3);
            command(entry,Action::Pause); step(entry,playback);
            command(entry,Action::Seek,0); step(entry,playback);
            check(entry.snapshot().position==0 && playback.paused,"paused editing may inspect outside the selection");
            command(entry,Action::Resume); step(entry,playback);
            check(entry.snapshot().position==17 && playback.position==17 && !playback.paused && !entry.snapshot().rewinding,
                  "Play from zero must immediately start at buffered IN");
            playback.audio_seeks.clear(); command(entry,Action::Seek,8); step(entry,playback);
            check(entry.snapshot().position==17 && playback.position==17 && entry.snapshot().loops==0,
                  "a running seek before IN must clamp without counting an OUT return");
            check(playback.audio_seeks==std::vector<float>{17.f},"constrained seek must align audio once without briefly visiting the invalid target");
            command(entry,Action::SetIn,25); step(entry,playback);
            check(entry.snapshot().position==22 && entry.snapshot().in==25,"moving IN ahead during playback must constrain the live position");
            command(entry,Action::SetLoop,0); command(entry,Action::Seek,0); step(entry,playback);
            check(playback.position==0 && !playback.paused,"Loop off permits ordinary playback from zero");
            command(entry,Action::SetLoop,1); step(entry,playback);
            check(playback.position==22,"enabling Loop during playback before IN must align immediately");
            command(entry,Action::Pause); command(entry,Action::Seek,60); step(entry,playback);
            command(entry,Action::Reverse); step(entry,playback);
            check(playback.position==43 && playback.reversing && !entry.snapshot().paused && !entry.snapshot().rewinding,
                  "reverse entry beyond OUT must immediately align to the buffered end");
            entry.end(playback);
            begin(entry,playback); command(entry,Action::Pause); command(entry,Action::Seek,0); step(entry,playback);
            playback.fail_seek=true; command(entry,Action::Resume); step(entry,playback);
            check(!entry.snapshot().ready && playback.paused && entry.suppress_result(),"failed IN alignment must pause and retain result protection");
            entry.end(playback);
        }
        cross_out(s,g);
        check(s.snapshot().rewinding && s.snapshot().position>20 && s.snapshot().position<40.01f,
              "default Loop must visibly return instead of teleporting");
        check(g.operations[0]=='M' && g.operations[1]=='P' && g.audio_seeks.empty(),
              "mute/pause must precede chart motion; audio must not seek every frame");
        finish_return(s,g);
        check(g.chart_seeks.size()>8,"loop return must show multiple intermediate frames");
        float previous=40.01f-g.chart_seeks.front();
        for(std::size_t i=1;i<g.chart_seeks.size();++i) {
            const float distance=g.chart_seeks[i-1]-g.chart_seeks[i];
            check(distance>0 && distance<previous,"loop return must slow down with entry's cubic ease-out");
            previous=distance;
        }
        check(g.position==20 && s.snapshot().position==20 && g.paused && g.muted && s.snapshot().paused,
              "default Loop must stop silently at IN");
        check(g.audio_seeks==std::vector<float>{20.f} && s.snapshot().loops==1 && g.speed==.5f && s.suppress_result(),
              "align audio exactly once, retain rate/result guard and count one completed return");
        const auto count=g.chart_seeks.size(); step(s,g);
        check(g.chart_seeks.size()==count && s.snapshot().loops==1,"paused return must not retrigger");
        command(s,Action::Resume); step(s,g);
        check(!g.paused && !g.muted,"Play after a one-shot return must restore audio");
        command(s,Action::SetRepeat,1); step(s,g);
        for(unsigned i=0;i<3;++i) {
            g.chart_seeks.clear(); g.audio_seeks.clear(); cross_out(s,g);
            check(s.snapshot().rewinding && g.muted && g.audio_seeks.empty(),"Repeat must use the same silent transition");
            finish_return(s,g);
            check(g.position==20 && !g.paused && !g.muted && !s.snapshot().paused && g.chart_seeks.size()>8 &&
                  g.audio_seeks==std::vector<float>{20.f} && s.snapshot().loops==i+2,
                  "Repeat must align at IN then resume each cycle without accumulating mute/pause");
        }
        cross_out(s,g); command(s,Action::SetRepeat,0); step(s,g); finish_return(s,g);
        check(g.paused && g.muted,"turning Repeat off during return must stop at IN");
        command(s,Action::Resume); command(s,Action::SetRepeat,1); step(s,g); cross_out(s,g);
        command(s,Action::SetLoop,0); step(s,g); finish_return(s,g);
        check(g.paused && g.muted,"turning Loop off during return must cancel automatic continuation");
        command(s,Action::Resume); step(s,g); const auto loops=s.snapshot().loops; cross_out(s,g);
        check(!s.snapshot().rewinding && !g.paused && s.snapshot().loops==loops,"Loop off must cross OUT normally");
        s.end(g);
        begin(s,g); command(s,Action::SetRepeat,1); command(s,Action::Reverse); step(s,g);
        g.position=19.99f; step(s,g);
        check(s.snapshot().rewinding && !g.reversing && g.muted,"reverse boundary must stop PCM during animated return");
        finish_return(s,g);
        check(g.position==40 && g.reversing && !g.paused && !g.muted && s.snapshot().reversing,
              "reverse Repeat must retain direction after returning to OUT");
        s.end(g);
        for(Action action:{Action::Pause,Action::Disable,Action::Finish}) {
            begin(s,g); command(s,Action::SetRepeat,1); step(s,g); cross_out(s,g);
            command(s,action); step(s,g);
            check(!s.snapshot().rewinding && s.suppress_result(),"cancelled return must retain result protection");
            if(action==Action::Pause) check(g.paused && g.muted,"Pause must stop a return at its visible position");
            else check(!g.muted,"Disable/Finish must release return mute");
            s.end(g); check(!g.muted,"native quick finish/end must release return mute");
        }
        for(unsigned failure=0;failure<3;++failure) {
            begin(s,g); command(s,Action::SetRepeat,1); step(s,g);
            if(failure==0) g.fail_chart=true;
            if(failure==1) g.fail_seek=true;
            if(failure==2) g.fail_resume=true;
            cross_out(s,g); finish_return(s,g);
            check(!s.snapshot().ready && g.paused && g.muted && s.suppress_result() && s.snapshot().loops==0,
                  "return/align/resume failure must remain silent and protected without counting success");
            s.end(g); check(!g.muted,"failed return teardown must restore bus ownership");
        }
        begin(s,g); check(!s.snapshot().repeat,"next track must reset Repeat"); s.end(g);
        // Buffered ranges preserve the editable marks and the existing animation.
        Session defaults;
        check(defaults.snapshot().loop_before==1 && defaults.snapshot().loop_after==1,"new sessions default to one second on both sides");
        begin(s,g); s.set_loop_buffers(5,5);
        g.position=40.01f; step(s,g);
        check(!s.snapshot().rewinding,"OUT marker must not trigger before the post-buffer has elapsed");
        g.position=44.99f; step(s,g); check(!s.snapshot().rewinding,"post-buffer runs to its complete limit");
        g.position=45.01f; step(s,g); finish_return(s,g);
        check(g.position==15 && g.paused && g.muted && s.snapshot().in==20 && s.snapshot().out==40,"buffered Loop returns paused at IN minus lead-in without moving marks");
        command(s,Action::SetRepeat,1); command(s,Action::Resume); step(s,g);
        g.position=45.01f; step(s,g); finish_return(s,g);
        check(g.position==15 && !g.paused && !g.muted,"buffered Repeat resumes from the lead-in");
        command(s,Action::Seek,35); command(s,Action::Reverse); step(s,g);
        g.position=19.99f; step(s,g); check(!s.snapshot().rewinding,"reverse playback must play through the lead-in");
        g.position=14.99f; step(s,g); finish_return(s,g);
        check(g.position==45 && g.reversing && !g.paused,"reverse Repeat returns to the end plus tail buffer");
        s.end(g); begin(s,g); s.set_loop_buffers(3,7);
        g.position=46.99f; step(s,g); check(!s.snapshot().rewinding,"independent trailing buffer is honored");
        g.position=47.01f; step(s,g); finish_return(s,g);
        check(g.position==17 && g.paused,"independent start buffer is honored");
        s.set_loop_buffers(0,0); command(s,Action::Resume); step(s,g); cross_out(s,g); finish_return(s,g);
        check(g.position==20,"zero buffers restore exact IN/OUT behavior immediately");
        s.set_loop_buffers(-1,5); s.set_loop_buffers(std::numeric_limits<float>::infinity(),5);
        check(s.snapshot().loop_before==0 && s.snapshot().loop_after==0,"invalid buffers must not replace active values");
        s.set_loop_buffers(500,500);
        check(loop_range(s.snapshot()).start==0 && loop_range(s.snapshot()).end==180,"buffers must clamp to both song ends");
        command(s,Action::Resume); step(s,g); g.position=179.99f; step(s,g);
        check(!s.snapshot().rewinding,"clamped tail plays through the end");
        g.position=180; step(s,g); finish_return(s,g);
        check(g.position==0 && g.paused && s.suppress_result(),"song-end clamp returns safely to zero and retains result guard");
        s.end(g); s.begin({"next.vox",2,3,120.f},true);
        check(s.snapshot().loop_before==500 && s.snapshot().loop_after==500,"preferences persist across tracks"); s.end(g);
        begin(s,g); s.set_loop_buffers(3,3);
        for(const auto [position,gain]:std::initializer_list<std::pair<float,float>>{{17.f,0.f},{18.5f,.5f},{20.f,1.f},{30.f,1.f},{40.f,1.f},{41.5f,.5f}}) {
            g.position=position; step(s,g);
            check(std::fabs(g.gain-gain)<.0001f,"playback must fade only inside the actual lead-in/tail");
        }
        command(s,Action::Seek,30); command(s,Action::Reverse); step(s,g);
        g.position=41.5f; step(s,g); check(std::fabs(g.gain-.5f)<.0001f,"reverse audio uses the same position envelope");
        s.set_loop_buffers(0,0); g.position=30; step(s,g); check(g.gain==1,"zero buffers have no fades");
        s.set_loop_buffers(3,3); command(s,Action::SetLoop,0); step(s,g); g.position=18; step(s,g);
        check(g.gain==1,"Loop off restores full original gain"); s.end(g); check(g.gain==1,"track teardown restores native level");
        State clipped; clipped.enabled=clipped.loop=true; clipped.track.duration=10; clipped.in=1; clipped.out=9;
        clipped.position=.5f; check(std::fabs(loop_gain(clipped)-.5f)<.0001f,"truncated lead-in scales to actual available duration");
        clipped.position=9.5f; check(std::fabs(loop_gain(clipped)-.5f)<.0001f,"truncated tail scales to actual available duration");
        AudioEnvelope envelope; float samples[]{1,-1,1,-1,1,-1,1,-1};
        envelope.apply(samples,0);
        check(samples[0]==.75f && samples[1]==-.75f && samples[6]==0 && samples[7]==0,"audio block fade preserves stereo and reaches silence");
        std::fill(std::begin(samples),std::end(samples),1.f); envelope.apply(samples,1);
        check(samples[0]==.25f && samples[6]==1 && samples[7]==1,"audio block fade restores original level smoothly");
        std::cout<<"Loop/Repeat silent ease-out, buffered boundaries, reverse, cancellation and failure scenarios passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
