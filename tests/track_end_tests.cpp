#include "core/session.hpp"
#include "game/game_policy.hpp"
#include "native/profile.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
using namespace training;
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
struct Probe final:Backend {
    bool paused{},complete{},query_fail{},seek_fail{},pause_fail{},finished{},reversing{},muted{},finish_fail{};
    float position{}; unsigned seeks{},resets{};
    bool pause(bool value) override { if(value && pause_fail) return false; paused=value; return true; }
    bool seek(float value) override { if(seek_fail) return false; position=value; complete=false; ++seeks; return true; }
    bool seek_chart(float value) override { position=value; complete=false; return true; }
    bool rate(float) override { return true; }
    bool finish() override { if(finish_fail) return false; finished=true; return true; }
    bool track_complete(bool& value) override { value=complete; return !query_fail; }
    bool reset_gauge() override { ++resets; return true; }
    bool reverse(bool value) override { reversing=value; return true; }
    bool mute(bool value) override { muted=value; return true; }
};
void setup(Session& s,Probe& b) {
    Track track; track.duration=100; track.gauge_ready=true;
    auto clip=std::make_shared<PcmClip>(); clip->samples.resize(2);
    track.reverse_audio=std::make_shared<ReverseAsset>(clip);
    b={}; s.set_loop_buffers(0,0); s.begin(track,true);
    s.submit({s.snapshot().generation,Action::Enable}); s.tick(0,false,b,.05f);
    s.submit({s.snapshot().generation,Action::Resume}); s.tick(0,b.paused,b);
    check(!s.snapshot().paused && !s.snapshot().rewinding,"fixture must finish entry and start playback");
}
void command(Session& s,Probe& b,Action a,float value=0) {
    s.submit({s.snapshot().generation,a,value}); s.tick(b.position,b.paused,b);
}
void session_cases() {
    Session s; Probe b; Track track; track.duration=100; s.begin(track,true);
    s.tick(101,false,b);
    check(!s.native_exit_locked(),"ordinary gameplay must retain native exit controls");
    check(!s.intercept_track_end(101,b) && !b.paused && !s.suppress_result(),"ordinary gameplay end must remain native");
    setup(s,b); s.tick(99,false,b);
    check(s.native_exit_locked(),"enabling training must lock native exits");
    check(!s.intercept_track_end(99,b),"unfinished track must keep native play state");
    const auto seeks=b.seeks,resets=b.resets;
    s.tick(100.1f,false,b);
    check(s.snapshot().at_end && s.snapshot().position==100 && s.snapshot().paused && b.paused &&
          s.snapshot().playing && s.suppress_result() && !b.finished,"natural end must pause without ending the protected track");
    check(b.seeks==seeks && b.resets==resets,"end pause must preserve native final judgments and blood");
    check(s.intercept_track_end(100.1f,b),"hold must not depend on completion-animation acknowledgment");
    for(unsigned i=0;i<10;++i) { s.tick(100.1f,true,b); check(s.intercept_track_end(100.1f,b),"end hold must persist"); }
    check(b.seeks==seeks && b.resets==resets && !b.finished,"holding must not rebuild judgments or trigger result routing");
    b.position=100.1f; command(s,b,Action::Resume);
    check(!s.snapshot().at_end && !s.snapshot().paused && s.snapshot().position==0 && b.position==0 &&
          b.seeks==seeks+1 && b.resets==resets+1 && !b.paused && s.suppress_result(),"Play at the end must restart at zero, reset blood and resume");
    s.tick(100.1f,false,b); b.position=100.1f;
    command(s,b,Action::Seek,45);
    check(!s.snapshot().at_end && s.snapshot().paused && s.snapshot().position==45 && !s.intercept_track_end(45,b),
          "seeking away from the held end must restore normal paused editing");
    setup(s,b); b.complete=true;
    const auto before=b.seeks;
    check(s.intercept_track_end(99.9f,b) && s.snapshot().at_end && b.paused && b.seeks==before,
          "native completion before the nominal timestamp must also hold without seeking");
    command(s,b,Action::Finish);
    check(b.finished && s.snapshot().finish_requested && !s.native_exit_locked() && !s.snapshot().enabled && !s.snapshot().at_end && !s.intercept_track_end(101,b) && s.suppress_result(),
          "explicit Finish must release the hold and retain score protection");
    command(s,b,Action::Enable);
    check(s.snapshot().finish_requested && !s.snapshot().enabled && !s.native_exit_locked(),"queued Enable cannot revive a track already leaving through Finish");
    setup(s,b); command(s,b,Action::SetLoop,1); b.complete=true;
    check(s.intercept_track_end(100,b) && !s.snapshot().at_end && !s.snapshot().paused,
          "Loop must own its return transport while native natural exit is held");
    s.tick(100,false,b,.05f);
    check(s.snapshot().rewinding && !s.snapshot().at_end,"Loop at song end must keep its existing rewind");
    for(unsigned i=0;i<60 && s.snapshot().rewinding;++i) s.tick(b.position,b.paused,b,.05f);
    check(s.snapshot().position==0 && s.snapshot().paused && !s.snapshot().at_end,"default Loop must return and pause");
    setup(s,b); s.tick(100,false,b); command(s,b,Action::Reverse); b.complete=true;
    check(s.intercept_track_end(100,b) && s.snapshot().reversing && !s.snapshot().paused && !s.snapshot().at_end,
          "reverse from the end must remain playable while exit is held");
    setup(s,b); b.query_fail=true;
    check(s.intercept_track_end(50,b) && s.snapshot().paused && !s.snapshot().ready && s.suppress_result(),
          "invalid native completion context must pause and fail closed");
    command(s,b,Action::Finish); check(b.finished,"Finish must remain available after end-control failure");
    setup(s,b); b.pause_fail=true; s.tick(100,false,b);
    check(s.snapshot().at_end && !s.snapshot().ready && s.intercept_track_end(100,b),"failed pause must still block native exit");
    setup(s,b); s.tick(100,false,b); b.seek_fail=true; command(s,b,Action::Resume);
    check(s.snapshot().at_end && s.snapshot().paused && !s.snapshot().ready && s.suppress_result(),"failed restart must retain the end hold");
    setup(s,b); s.tick(100,false,b); command(s,b,Action::Disable);
    check(s.intercept_track_end(100,b) && s.snapshot().at_end && s.native_exit_locked() && s.suppress_result(),
          "internal transport cleanup must not authorize native exits or release the end hold");
    b.finish_fail=true; command(s,b,Action::Finish);
    check(s.native_exit_locked() && !s.snapshot().finish_requested && !b.finished,"failed Finish must not grant exit permission");
    b.finish_fail=false; command(s,b,Action::Finish);
    check(!s.native_exit_locked() && b.finished,"Finish retry must grant permission only after success");
    s.end(b); s.begin(track,true);
    check(!s.snapshot().at_end && !s.suppress_result() && !s.native_exit_locked() && !s.snapshot().finish_requested,
          "a new song must start without previous end state or exit permission");
}
template<class T> T& at(std::uintptr_t p,std::size_t o) { return *reinterpret_cast<T*>(p+o); }
void native_cases(const wchar_t* path) {
    auto module=LoadLibraryExW(path,nullptr,DONT_RESOLVE_DLL_REFERENCES);
    check(module!=nullptr,"reference game cannot be loaded");
    try {
        std::string reason; check(native::validate(module,reason),reason.c_str());
        const auto image=reinterpret_cast<std::uintptr_t>(module); const auto& a=native::addresses();
        alignas(8) std::array<std::byte,2048> world{};
        alignas(8) std::array<std::byte,512> player{},objects{};
        const auto w=reinterpret_cast<std::uintptr_t>(world.data()),p=reinterpret_cast<std::uintptr_t>(player.data());
        const auto o=reinterpret_cast<std::uintptr_t>(objects.data());
        at<std::uintptr_t>(w,0)=image+a.world_vtable; at<std::uintptr_t>(w,200)=p;
        at<std::uintptr_t>(p,0)=image+a.fumen_player_vtable; at<std::uintptr_t>(p,24)=o;
        const auto old_mode=at<int>(image,a.game_mode);
        auto& forced=at<unsigned char>(w,native::world_layout().forced_end);
        for(int mode:{0,1,3,6}) {
            at<int>(image,a.game_mode)=mode; forced=1;
            check(!suppress_native_finish(image,w,false) && forced==1,"ordinary native finish must pass in every mode");
            check(suppress_native_finish(image,w,true) && forced==0,"training must consume native quick finish in every mode");
            check(!suppress_native_finish(image,w,true) && forced==0,"idle filtering must not create finish requests");
            check(prepare_training_finish(image,w),"valid world must accept quiet training finish");
            check(at<std::uintptr_t>(w,72)==image+a.quiet_exit_state && at<std::uintptr_t>(w,80)==0 &&
                  at<int>(w,100)==0 && at<unsigned char>(w,104)==1,
                  "Finish must queue native final cleanup directly, bypassing Track Crash/Complete presentation");
            check(forced==1 && !suppress_native_finish(image,w,false) && forced==1,"authorized Finish must retain its native end flag");
        }
        at<int>(image,a.game_mode)=old_mode; forced=0;
        bool complete=true;
        check(native_track_complete(image,w,complete) && !complete,"native getter must report incomplete objects");
        at<unsigned char>(o,372)=1;
        check(native_track_complete(image,w,complete) && !complete,"native end must wait for UC/PUC animation acknowledgment");
        at<unsigned char>(p,152)=1;
        check(native_track_complete(image,w,complete) && complete,"native completion requires both the chart and animation acknowledgment");
        at<unsigned char>(o,372)=0;
        check(native_track_complete(image,w,complete) && !complete,"native chart reset must make replay incomplete again");
        at<std::uintptr_t>(p,0)+=8;
        check(!native_track_complete(image,w,complete) && !complete,"wrong player interface must be rejected");
        at<std::uintptr_t>(p,0)=image+a.fumen_player_vtable; at<std::uintptr_t>(p,24)=0;
        check(!native_track_complete(image,w,complete),"missing object player must be rejected");
        check(!native_track_complete(image,0,complete),"missing world must be rejected");
        check(!suppress_native_finish(image,0,true),"missing world must not be written by exit filtering");
        check(!prepare_training_finish(image,0),"missing world must reject quiet finish");
        at<std::uintptr_t>(w,0)+=8;
        check(!prepare_training_finish(image,w),"wrong world interface must reject quiet finish");
        std::cout<<"Native completion/announcement getter passed: "<<reason<<'\n';
    } catch(...) { FreeLibrary(module); throw; }
    FreeLibrary(module);
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc==2) native_cases(argv[1]);
        else session_cases();
        std::cout<<"Track-end hold and restart scenarios passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
