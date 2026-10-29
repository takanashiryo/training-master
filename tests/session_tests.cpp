#include "core/session.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace training;
struct Game final:Backend {
    float pos{},speed{1}; bool paused{},finished{},fail{},reversing{},muted{}; int seeks{};
    bool pause(bool p) override { paused=p; return true; }
    bool seek(float p) override { if(fail) return false; pos=p; ++seeks; return true; }
    bool rate(float r) override { speed=r; return true; }
    bool finish() override { finished=true; return true; }
    bool reverse(bool value) override { reversing=value; return !fail; }
    bool sync_reverse_chart(float p) override { pos=p; return !fail; }
    bool seek_chart(float p) override { pos=p; return !fail; }
    bool mute(bool value) override { muted=value; return true; }
    bool fixed_speed{},lane_fail{};
    bool lane_speed(bool value) override { if(lane_fail) return false; fixed_speed=value; return true; }
    void release_lane_speed() override { fixed_speed=false; }
};
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
void command(Session& s,Action a,float v=0) { s.submit({s.snapshot().generation,a,v}); }
void tick(Session& s,Game& g,float pos) {
    // These transport regressions operate after startup. Entry timing, intermediate
    // positions and audio synchronization are exercised separately by training_entry.
    const bool enabling=!s.snapshot().enabled;
    const int seeks=g.seeks;
    g.pos=pos; s.tick(pos,g.paused,g,.016f);
    if (enabling && s.snapshot().ready && s.snapshot().enabled && g.muted &&
        (s.snapshot().rewinding || s.snapshot().position==0)) {
        for(unsigned i=0;i<30 && s.snapshot().rewinding;++i) s.tick(g.pos,g.paused,g,.05f);
        command(s,Action::Resume); s.tick(g.pos,g.paused,g,0);
        g.seeks=seeks; // Count the transport operations under test, excluding entry alignment.
    }
    // Intermediate boundary frames are covered separately by training_loop.
    if (!enabling)
        for(unsigned i=0;i<30 && s.snapshot().rewinding;++i) s.tick(g.pos,g.paused,g,.05f);
}
void begin(Session& s,bool ready=true) { s.set_loop_buffers(0,0); s.begin({"test.vox",12,3,120.f,1000,100},ready); }
int main() {
    try {
        Session s; Game g; begin(s);
        check(!s.snapshot().enabled && !s.suppress_result(),"must default off");
        command(s,Action::Seek,50); tick(s,g,0); check(g.seeks==0,"off ignores transport");
        command(s,Action::Enable); command(s,Action::SetIn,20); command(s,Action::SetOut,40);
        command(s,Action::SetLoop,1); command(s,Action::SetRate,.5f); tick(s,g,22);
        check(s.suppress_result() && g.speed==.5f,"enable latches no-result and sets audio rate");
        tick(s,g,40.01f); check(g.pos==20 && s.snapshot().loops==1,"loop must return to IN");
        command(s,Action::Pause); tick(s,g,21); check(g.paused,"pause");
        tick(s,g,45); check(g.seeks==1,"paused must not loop");
        command(s,Action::Seek,30); tick(s,g,45); check(g.pos==30 && g.paused,"paused seek stays paused");
        command(s,Action::SetIn,50); command(s,Action::SetOut,10); tick(s,g,30);
        check(s.snapshot().in==20 && s.snapshot().out==40,"reject inverted region");
        command(s,Action::SetRate,std::numeric_limits<float>::quiet_NaN()); tick(s,g,30);
        check(s.snapshot().rate==.5f,"reject NaN");
        command(s,Action::SetRate,2.f); tick(s,g,30);
        check(s.snapshot().rate==2.f && g.speed==2.f,"2x reaches the playback backend");
        command(s,Action::SetRate,2.01f); tick(s,g,30);
        check(s.snapshot().rate==2.f && g.speed==2.f,"rates above 2x must be rejected");
        command(s,Action::Disable); tick(s,g,30);
        check(!g.paused && g.speed==1 && s.suppress_result(),"disable restores transport, not eligibility");
        command(s,Action::Finish); tick(s,g,30); check(g.finished,"finish works after disable");
        s.end(g); check(s.suppress_result(),"teardown must preserve result latch");
        s.begin({"test.vox",12,3,120.f,1000,100},true,true);
        check(s.suppress_result() && !s.snapshot().enabled,"native retry retains taint while controls default off");
        const auto old=s.snapshot().generation;
        begin(s); s.submit({old,Action::Enable}); tick(s,g,0);
        check(!s.suppress_result(),"stale command cannot taint next track");
        command(s,Action::Enable); command(s,Action::SetOut,10); tick(s,g,10);
        const auto seeks_without_loop=g.seeks;
        tick(s,g,11);
        check(!g.paused && !s.snapshot().paused && g.seeks==seeks_without_loop,
              "loop off must continue through OUT without pause or seek");
        command(s,Action::Pause); tick(s,g,11);
        command(s,Action::Restart); tick(s,g,10); check(g.pos==0 && g.paused,"restart retains pause state");
        command(s,Action::Resume); command(s,Action::SetLoop,1); tick(s,g,0);
        g.fail=true; tick(s,g,11);
        check(g.paused && !s.snapshot().ready && s.suppress_result(),"seek failure pauses and keeps no-result");
        g.fail=false; command(s,Action::Finish); tick(s,g,11); check(g.finished,"failure recovery finish");
        begin(s,false); command(s,Action::Enable); tick(s,g,0); check(!s.snapshot().enabled,"missing hooks reject enable");
        begin(s); command(s,Action::Enable); command(s,Action::Seek,-5); tick(s,g,0); check(g.pos==0,"seek clamps below start");
        command(s,Action::Seek,500); tick(s,g,0); check(g.pos==120,"seek clamps beyond end");
        s.result_blocked(); s.end(g); begin(s); check(s.snapshot().blocked_results==1,"diagnostics persist across tracks");
        s.begin({"test.vox",12,3,120.f,1000,100,true},true);
        command(s,Action::Enable); command(s,Action::SetLoop,1); command(s,Action::SetRate,.5f);
        command(s,Action::Pause); tick(s,g,30);
        command(s,Action::Seek,60); command(s,Action::Enable);
        const auto seeks_before_exit=g.seeks;
        s.end(g); tick(s,g,125);
        check(!g.paused && g.speed==1 && !s.snapshot().playing && !s.snapshot().enabled,
              "native finish releases training pause and rate");
        check(g.seeks==seeks_before_exit && s.suppress_result(),
              "native finish discards queued transport and retains score guard");
        begin(s); command(s,Action::Enable);
        s.submit({s.snapshot().generation,Action::SetRegion,20,40}); tick(s,g,0);
        s.submit({s.snapshot().generation,Action::SetRegion,60,80}); tick(s,g,0);
        check(s.snapshot().in==60 && s.snapshot().out==80,"moving past old OUT must atomically preserve both endpoints");
        s.submit({s.snapshot().generation,Action::SetRegion,0,20}); tick(s,g,21);
        check(s.snapshot().in==0 && s.snapshot().out==20 && !g.paused,"moving to track start must not pause while loop is off");
        s.submit({s.snapshot().generation,Action::SetRegion,100,120}); tick(s,g,0);
        s.submit({s.snapshot().generation,Action::SetRegion,110,110.01f}); tick(s,g,0);
        s.submit({s.snapshot().generation,Action::SetRegion,110,std::numeric_limits<float>::quiet_NaN()}); tick(s,g,0);
        check(s.snapshot().in==100 && s.snapshot().out==120,"invalid region pairs must leave the complete selection unchanged");
        command(s,Action::SetLoop,1); command(s,Action::SetRepeat,1); tick(s,g,121);
        check(g.pos==100 && s.snapshot().loops==1,"enabled selection must loop at OUT");
        command(s,Action::SetLoop,0); const auto saved_seeks=g.seeks; tick(s,g,121);
        check(g.seeks==saved_seeks && g.paused && s.snapshot().at_end && s.snapshot().in==100 && s.snapshot().out==120,
              "disabling loop at song end must retain selection and pause without rewinding");
        Track automatic{"test.vox",12,3,120.f,1000,100}; automatic.autoplay_ready=true;
        s.begin(automatic,true);
        check(!s.snapshot().autoplay,"AutoPlay defaults off for each track");
        command(s,Action::SetAutoplay,1); tick(s,g,1);
        check(s.snapshot().autoplay && s.snapshot().enabled && s.suppress_result(),
              "AutoPlay must enable training and block results before native override");
        command(s,Action::SetAutoplay,0); tick(s,g,2);
        check(!s.snapshot().autoplay && s.snapshot().enabled && s.suppress_result(),"AutoPlay off must not restore result eligibility");
        command(s,Action::SetAutoplay,1); command(s,Action::Disable); tick(s,g,3);
        check(!s.snapshot().autoplay && !s.snapshot().enabled && s.suppress_result(),"disabling training must stop AutoPlay");
        command(s,Action::SetAutoplay,1); tick(s,g,4); s.end(g);
        check(!s.snapshot().autoplay && s.suppress_result(),"native quick finish/teardown stops AutoPlay and retains guard");
        s.begin(automatic,true); command(s,Action::SetAutoplay,1); command(s,Action::Finish); tick(s,g,5);
        check(!s.snapshot().autoplay && s.suppress_result(),"Finish Immediately stops AutoPlay");
        s.begin(automatic,true); check(!s.snapshot().autoplay && !s.suppress_result(),"next track resets AutoPlay and eligibility");
        s.begin(automatic,false); command(s,Action::SetAutoplay,1); tick(s,g,0);
        check(!s.snapshot().autoplay && !s.suppress_result(),"unsupported/multiplayer world cannot enable AutoPlay");
        begin(s); command(s,Action::SetAutoplay,1); tick(s,g,0);
        check(!s.snapshot().autoplay && !s.suppress_result(),"missing native AutoPlay discovery must reject enable");
        Track view=automatic; view.camera_ready=true;
        s.begin(view,true);
        check(!s.camera_requested(),"camera movement block defaults off");
        command(s,Action::SetCameraLocked,1); tick(s,g,1);
        check(s.camera_requested() && s.suppress_result() && !s.snapshot().autoplay,
              "camera block enables protected training without enabling AutoPlay");
        command(s,Action::SetCameraLocked,0); tick(s,g,2);
        check(!s.camera_requested() && s.snapshot().enabled && s.suppress_result(),"unlock retains score guard");
        command(s,Action::SetCameraLocked,1); command(s,Action::Disable); tick(s,g,3);
        check(!s.camera_requested() && !s.snapshot().camera_locked,"disable releases camera block");
        command(s,Action::SetCameraLocked,1); tick(s,g,4); s.camera_unavailable();
        check(!s.camera_requested() && !s.snapshot().track.camera_ready && s.suppress_result(),
              "camera fault stops view writes while keeping protected training");
        command(s,Action::SetCameraLocked,1); tick(s,g,5);
        check(!s.camera_requested(),"failed camera binding cannot be re-enabled in the same track");
        s.begin(view,true); command(s,Action::SetCameraLocked,1); command(s,Action::Finish); tick(s,g,6);
        check(!s.camera_requested() && !s.snapshot().camera_locked && s.suppress_result(),"Finish releases camera block");
        s.begin(view,true); command(s,Action::SetCameraLocked,1); tick(s,g,7); s.end(g);
        check(!s.camera_requested() && !s.snapshot().camera_locked && s.suppress_result(),"native teardown releases camera block");
        s.begin(view,true); check(!s.snapshot().camera_locked && !s.suppress_result(),"next track resets camera option");
        s.begin(view,false); command(s,Action::SetCameraLocked,1); tick(s,g,0);
        check(!s.camera_requested() && !s.suppress_result(),"unsupported world cannot block camera movement");
        begin(s); command(s,Action::SetCameraLocked,1); tick(s,g,0);
        check(!s.camera_requested() && !s.suppress_result(),"missing camera binding cannot enable view override");
        auto pcm=std::make_shared<PcmClip>(); pcm->samples.resize(100,1);
        automatic.reverse_audio=std::make_shared<ReverseAsset>(pcm);
        s.begin(automatic,true); command(s,Action::Reverse); tick(s,g,30);
        check(!s.snapshot().reversing && !s.suppress_result(),"reverse must require enabled training");
        command(s,Action::Enable); command(s,Action::Reverse); tick(s,g,30);
        check(s.snapshot().reversing && !s.snapshot().paused && s.suppress_result() && g.reversing,"reverse must start and retain score guard");
        g.paused=true; tick(s,g,29);
        check(s.snapshot().position==29 && !s.snapshot().paused,"native pause must not stop logical reverse playback");
        command(s,Action::Pause); tick(s,g,28); check(s.snapshot().paused && s.snapshot().reversing,"pause must retain reverse direction");
        command(s,Action::Reverse); tick(s,g,28); check(!s.snapshot().paused && s.snapshot().reversing,"reverse button must resume backwards");
        command(s,Action::SetIn,20); command(s,Action::SetOut,40); command(s,Action::SetLoop,1);
        command(s,Action::SetRepeat,1); tick(s,g,19.99f);
        check(g.pos==40 && s.snapshot().position==40 && s.snapshot().loops==1,"reverse loop must wrap IN to OUT");
        command(s,Action::SetLoop,0); tick(s,g,0);
        check(s.snapshot().paused && s.snapshot().reversing,"reverse without loop must stop at song start");
        command(s,Action::Resume); tick(s,g,0);
        check(!s.snapshot().paused && !s.snapshot().reversing && !g.reversing && !g.paused,"play must restore native forward playback");
        command(s,Action::Reverse); command(s,Action::Disable); tick(s,g,25);
        check(!s.snapshot().reversing && !g.reversing && s.suppress_result(),"disable must release reverse without restoring score eligibility");
        command(s,Action::Enable); command(s,Action::Reverse); tick(s,g,25); s.end(g);
        check(!s.snapshot().reversing && !g.reversing && !g.paused,"teardown must stop reverse and release native pause");
        s.begin(automatic,true); command(s,Action::Enable); command(s,Action::Reverse); tick(s,g,25);
        command(s,Action::Finish); tick(s,g,24);
        check(!s.snapshot().reversing && !g.reversing && g.finished && s.suppress_result(),"Finish must release reverse and preserve score guard");
        s.begin(automatic,true); command(s,Action::Enable); command(s,Action::Reverse); tick(s,g,25);
        g.fail=true; tick(s,g,24); g.fail=false;
        check(!s.snapshot().reversing && !g.reversing && s.snapshot().paused && !s.snapshot().ready && s.suppress_result(),"reverse chart failure must stop reverse and keep results blocked");
        Track speed{"test.vox",12,3,120.f}; speed.lane_speed_ready=true;
        s.begin(speed,true); command(s,Action::SetLaneSpeedLocked,1); tick(s,g,1);
        check(s.snapshot().lane_speed_locked && s.snapshot().enabled && g.fixed_speed && s.suppress_result(),
              "Lane Speed must enable protected training before native display changes");
        command(s,Action::SetLaneSpeedLocked,0); tick(s,g,1);
        check(!s.snapshot().lane_speed_locked && !g.fixed_speed && s.suppress_result(),"unlock must retain result taint");
        command(s,Action::SetLaneSpeedLocked,1); command(s,Action::Disable); tick(s,g,1);
        check(!s.snapshot().lane_speed_locked && !g.fixed_speed,"Disable must restore native speed mapping");
        command(s,Action::SetLaneSpeedLocked,1); tick(s,g,1); s.end(g);
        check(!s.snapshot().lane_speed_locked && !g.fixed_speed && s.suppress_result(),"native teardown releases Lane Speed");
        s.begin(speed,true); command(s,Action::SetLaneSpeedLocked,1); command(s,Action::Finish); tick(s,g,1);
        check(!s.snapshot().lane_speed_locked && !g.fixed_speed && s.suppress_result(),"Finish releases Lane Speed");
        s.begin(speed,false); command(s,Action::SetLaneSpeedLocked,1); tick(s,g,1);
        check(!s.snapshot().lane_speed_locked && !s.suppress_result(),"unsupported worlds reject Lane Speed");
        begin(s); command(s,Action::SetLaneSpeedLocked,1); tick(s,g,1);
        check(!s.snapshot().lane_speed_locked && !s.suppress_result(),"missing native speed context rejects enable");
        s.begin(speed,true); g.lane_fail=true; command(s,Action::SetLaneSpeedLocked,1); tick(s,g,1);
        check(!s.snapshot().ready && s.snapshot().paused && s.suppress_result(),"native rebuild failure pauses with results blocked");
        g.lane_fail=false; command(s,Action::Finish); tick(s,g,1); s.end(g);
        s.begin(speed,true); check(!s.snapshot().lane_speed_locked && !s.suppress_result(),"next track resets speed lock");
        std::cout<<"All training session scenarios passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
