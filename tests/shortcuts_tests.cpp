#include "game/game_input.hpp"
#include "game/autoplay_resolver.hpp"
#include <windows.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace training;
namespace {
void check(bool good,const char* message) { if(!good) throw std::runtime_error(message); }
struct Game final:Backend {
    float position{},reverse_position{}; bool paused{},reversing{},fail_seek{},muted{},fail_chart{};
    unsigned seeks{},resets{};
    std::vector<float> chart_seeks;
    bool pause(bool value) override { paused=value; return true; }
    bool seek(float value) override { ++seeks; if(fail_seek) return false; position=value; if(reversing) reverse_position=value; return true; }
    bool seek_chart(float value) override {
        check(paused,"chart-only navigation must keep native playback paused");
        if(fail_chart) return false;
        position=value; chart_seeks.push_back(value); return true;
    }
    bool rate(float) override { return true; }
    bool reverse(bool value) override {
        if(value) reverse_position=position;
        else if(reversing) position=reverse_position;
        reversing=value; return true;
    }
    bool mute(bool value) override { muted=value; return true; }
    bool reset_gauge() override { ++resets; return true; }
    bool finish() override { return true; }
};
struct Fixture {
    Session session; Game game; GameShortcuts shortcuts;
    std::uint64_t now{};
    void command(Action a,float value=0) { session.submit({session.snapshot().generation,a,value}); }
    void tick(float dt=.025f) { session.tick(game.position,game.paused,game,dt); }
    Fixture() {
        Track t{"shortcuts.vox",1,3,180.f}; t.gauge_ready=true;
        auto clip=std::make_shared<PcmClip>(); clip->samples.resize(2,0);
        t.reverse_audio=std::make_shared<ReverseAsset>(clip);
        session.begin(t,true); command(Action::Enable); tick();
        for(int i=0;i<100 && session.snapshot().rewinding;++i) tick();
        command(Action::Seek,60); tick(); game.seeks=game.resets=0;
        poll({});
        game.chart_seeks.clear();
    }
    void poll(GameInput input,std::uint64_t elapsed=16) {
        now+=elapsed; shortcuts.poll(session,input,now); tick();
    }
    void tap() { poll({true,true,true,0}); poll({},100); }
    void finish_return() {
        for(unsigned i=0;i<100 && session.snapshot().rewinding;++i) tick();
        check(!session.snapshot().rewinding,"animated return must finish in bounded time");
    }
    void settle_scrub() {
        for(unsigned i=0;i<100 && session.snapshot().scrubbing;++i) tick();
        check(!session.snapshot().scrubbing,"knob smoothing must settle in bounded time");
    }
};
template<class T> struct RestoreValue {
    T* address; T before;
    RestoreValue(std::uintptr_t at,T value):address(reinterpret_cast<T*>(at)),before(*address) { *address=value; }
    ~RestoreValue() { *address=before; }
};
void native_input_conflict(const char* path) {
    const auto module=LoadLibraryExA(path,nullptr,DONT_RESOLVE_DLL_REFERENCES);
    check(module!=nullptr,"map native shortcut conflict fixture");
    struct Unload { HMODULE module; ~Unload() { FreeLibrary(module); } } unload{module};
    const auto resolved=autoplay_discovery::resolve(module);
    check(resolved && resolved.layout.input_device_slot && resolved.layout.input_check_update && resolved.layout.input_check_vtable,
          "native shortcut checker ABI must resolve");
    const auto& layout=resolved.layout;
    const auto rip=[&](std::initializer_list<unsigned char> prefix,unsigned length) {
        const auto code=reinterpret_cast<const unsigned char*>(layout.input_check_update);
        for(unsigned i=0;i+length<=256;++i) if(!std::memcmp(code+i,prefix.begin(),prefix.size())) {
            std::int32_t displacement{}; std::memcpy(&displacement,code+i+prefix.size(),4);
            return layout.input_check_update+i+length+displacement;
        }
        throw std::runtime_error("native checker fixture global reference not found");
    };
    // Only substitute the singleton and its already-initialized bit. This
    // executes the real mapped checker without invoking imports or DllMain.
    std::vector<unsigned char> device(0x8200);
    auto set=[&](std::size_t at,unsigned value) { std::memcpy(device.data()+at,&value,4); };
    RestoreValue<std::uintptr_t> io(layout.input_device_slot,reinterpret_cast<std::uintptr_t>(device.data()));
    const auto initialized=rip({0x8b,0x05},6),frame_time=rip({0xf3,0x48,0x0f,0x2a,0x35},9);
    RestoreValue<unsigned> initialized_guard(initialized,*reinterpret_cast<const unsigned*>(initialized)|1u);
    RestoreValue<std::int64_t> time_guard(frame_time,16000000);
    alignas(8) std::array<unsigned char,24> checker{};
    std::memcpy(checker.data(),&layout.input_check_vtable,sizeof(std::uintptr_t));
    const auto object=reinterpret_cast<std::uintptr_t>(checker.data());
    const auto original=reinterpret_cast<GameInputCheckUpdate>(layout.input_check_update);
    set(0x8168,1); set(0x817c,1); set(0x8180,1);
    original(object);
    check(checker[16]!=0,"native START + FX-L + FX-R sets the force-crash request even on its first frame");
    std::cout<<"Native first-frame chord reproduced force-crash request\n";
    GameInput input; check(read_game_input(layout.input_device_slot,input) && input.start && input.fx_left && input.fx_right,
                           "shortcut reader must retain raw chord while neutralizing native force-crash");
    check(update_game_input_check(original,object,layout.input_check_vtable,true) && checker[16]==0,
          "training-owned combination must clear native force-crash before the native player can fail all gauges");
    float held{}; std::memcpy(&held,checker.data()+20,4);
    check(held==0,"training must also clear native quick-retry hold duration");
    for(float position:{0.f,60.f}) {
        Fixture f; f.command(Action::Seek,position); f.command(Action::Resume); f.tick();
        for(unsigned n=0;n<4;++n) {
            const bool was_paused=f.game.paused;
            f.shortcuts.poll(f.session,input,f.now+=16); f.tick();
            check(update_game_input_check(original,object,layout.input_check_vtable,true) && checker[16]==0,
                  "owned native player update must not receive a forced failure on pause or resume");
            check(f.game.paused!=was_paused && !f.session.snapshot().rewinding && f.game.position==position,
                  "native chord pause/resume at zero and mid-song must keep the position without a failure return");
            set(0x8168,0); set(0x817c,0); set(0x8180,0);
            check(read_game_input(layout.input_device_slot,input),"native release sample");
            f.shortcuts.poll(f.session,input,f.now+=1500); f.tick();
            set(0x8168,1); set(0x817c,1); set(0x8180,1);
            check(read_game_input(layout.input_device_slot,input),"native new chord sample");
        }
    }
    check(update_game_input_check(original,object,layout.input_check_vtable,false) && checker[16]!=0,
          "ordinary gameplay must retain the native force-crash control");
    set(0x8168,0); set(0x817c,0); set(0x8180,0);
    check(update_game_input_check(original,object,layout.input_check_vtable,false) && checker[16]==0,
          "ordinary native release must still reset the checker");
    std::cout<<"Native shortcut conflict scenarios passed\n";
}
}
int main(int argc,char** argv) {
    try {
        {
            Fixture f; f.command(Action::SetIn,20); f.command(Action::SetOut,90); f.tick();
            f.poll({false,true,false,0,true,false});
            check(f.session.snapshot().loop && f.session.snapshot().repeat && f.session.snapshot().in==20 &&
                  f.session.snapshot().out==90 && f.game.paused && f.game.position==60 && !f.game.seeks,
                  "FX-L + BT-A must cycle off to repeat without marking IN or moving the paused cursor");
            f.poll({false,true,false,0,true,false},1500);
            check(f.session.snapshot().repeat,"holding the loop chord must cycle only once");
            f.poll({false,true,false,0,false,false}); f.poll({false,true,false,0,true,false});
            check(f.session.snapshot().loop && !f.session.snapshot().repeat && f.session.snapshot().in==20,
                  "repressing BT-A with FX-L held must cycle repeat to once without marking IN");
            f.poll({}); f.poll({false,true,false,0,true,false});
            check(!f.session.snapshot().loop && !f.session.snapshot().repeat,"once must cycle back to off");
            f.poll({false,false,false,0,true,false});
            check(f.session.snapshot().in==20,"releasing FX-L with BT-A held must not mark IN");
            f.poll({}); f.poll({false,false,false,0,true,false});
            check(f.session.snapshot().in==60,"BT-A alone must still mark IN after releasing the chord");
        }
        {
            Fixture f; f.command(Action::Resume); f.tick();
            f.poll({false,true,false,0,false,false}); f.poll({false,true,false,0,true,false});
            check(!f.session.snapshot().loop && !f.game.paused && f.session.snapshot().in==0,
                  "playing must ignore the loop shortcut with FX-L pressed first");
            f.poll({}); f.poll({false,false,false,0,true,false}); f.poll({false,true,false,0,true,false});
            check(!f.session.snapshot().loop,"playing must ignore the loop shortcut with BT-A pressed first");
            f.command(Action::Pause); f.tick(); f.poll({false,true,false,0,true,false});
            check(!f.session.snapshot().loop,"a loop chord held from playback must not fire on pause");
            f.poll({}); f.poll({false,true,false,0,true,false});
            check(f.session.snapshot().loop && f.session.snapshot().repeat,"a fresh paused chord must cycle loop mode");
            f.poll({}); f.poll({false,false,false,0,true,false}); f.poll({false,true,false,0,true,false});
            check(f.session.snapshot().loop && !f.session.snapshot().repeat,
                  "the paused loop chord must recognize either button press order");
        }
        {
            Fixture f; f.command(Action::Resume);
            f.shortcuts.poll(f.session,{false,true,false,0,true,false},f.now+=16); f.tick();
            check(!f.session.snapshot().loop && !f.game.paused,
                  "a queued resume must prevent the paused-only loop command from changing modes");
            f.command(Action::Pause); f.command(Action::CycleLoopPaused); f.tick();
            check(f.session.snapshot().loop && f.session.snapshot().repeat,"paused-only loop commands must work after pause");
            f.command(Action::ReturnToStart); f.command(Action::CycleLoopPaused); f.tick();
            check(f.session.snapshot().repeat,"animated return must reject paused-only loop commands");
        }
        {
            Fixture f; f.command(Action::Disable); f.tick();
            f.poll({false,true,false,0,true,false});
            check(!f.session.snapshot().loop,"disabled training must ignore the loop shortcut");
            f.command(Action::Enable); f.tick(); f.finish_return(); f.poll({false,true,false,0,true,false});
            check(!f.session.snapshot().loop,"enabling training must not synthesize a held loop chord");
            f.poll({}); f.poll({false,true,false,0,true,false});
            check(f.session.snapshot().repeat,"a fresh loop chord must work after enabling training");
        }
        {
            Fixture f; f.command(Action::ReturnToStart); f.tick();
            f.poll({false,true,false,0,true,false}); f.finish_return(); f.poll({false,true,false,0,true,false});
            check(!f.session.snapshot().loop,"rewinding and return completion must not synthesize a loop press");
            f.poll({}); f.poll({true,true,true,0,true,false});
            check(!f.session.snapshot().loop && !f.game.paused,"the playback chord must take priority over the loop chord");
            f.poll({false,true,false,0,true,false});
            check(!f.session.snapshot().loop,"releasing the playback chord must not leak a pending loop action");
            f.shortcuts.reset(); f.poll({false,true,false,0,true,false});
            check(!f.session.snapshot().loop,"the first input sample must not invent a loop press");
        }
        {
            Fixture f;
            f.poll({false,false,false,0,true,false});
            check(f.session.snapshot().in==60 && f.session.snapshot().out==180 && f.game.paused && !f.game.seeks,
                  "BT-A must mark the paused cursor as IN without moving playback");
            f.command(Action::Seek,90); f.tick();
            f.poll({false,false,false,0,true,true});
            check(f.session.snapshot().in==60 && f.session.snapshot().out==90 && f.game.paused,
                  "held BT-A must not repeat while a new BT-D press marks OUT");
            f.poll({}); f.command(Action::Seek,75); f.tick();
            f.poll({false,false,false,0,true,false});
            check(f.session.snapshot().in==75,"release and repress must mark the new cursor");
            f.poll({false,false,false,0,false,true});
            check(f.session.snapshot().out==90,"marker shortcuts must retain the minimum loop range validation");
        }
        {
            Fixture f; f.command(Action::Resume); f.tick();
            f.poll({false,false,false,0,true,true});
            check(f.session.snapshot().in==0 && f.session.snapshot().out==180,"playing must ignore both marker keys");
            f.command(Action::Pause); f.tick(); f.poll({false,false,false,0,true,true});
            check(f.session.snapshot().in==0 && f.session.snapshot().out==180,"buttons held from playback must not fire on pause");
            f.poll({}); f.poll({false,false,false,0,true,false});
            check(f.session.snapshot().in==60,"a fresh paused press must work after playback");
        }
        {
            Fixture f; f.command(Action::Seek,72); f.command(Action::MarkIn); f.tick();
            check(f.session.snapshot().in==72,"queued marks must resolve the latest game-thread cursor after a seek");
            f.game.position=96; f.command(Action::MarkOut); f.tick();
            check(f.session.snapshot().out==96,"marks must use the sampled native cursor rather than a stale input snapshot");
            f.command(Action::Resume); f.command(Action::MarkIn); f.command(Action::MarkOut); f.tick();
            check(f.session.snapshot().in==72 && f.session.snapshot().out==96,"queued resume must prevent subsequent marker commands");
        }
        {
            Fixture f; f.poll({false,false,false,4096});
            const auto visible=f.session.snapshot().position;
            f.poll({false,false,false,4096,true,false});
            check(f.session.snapshot().in==visible && visible<60.125f,
                  "a mark during knob smoothing must use the visible cursor, not the pending target");
        }
        {
            Fixture f; f.command(Action::Disable); f.tick();
            f.poll({false,false,false,0,true,true});
            f.command(Action::MarkIn); f.command(Action::MarkOut); f.tick();
            check(f.session.snapshot().in==0 && f.session.snapshot().out==180,"disabled training must ignore marker commands");
            f.command(Action::Enable); f.tick(); f.finish_return();
            f.poll({false,false,false,0,true,true});
            check(f.session.snapshot().in==0 && f.session.snapshot().out==180,"enabling training must not synthesize held marker presses");
        }
        {
            Fixture f; f.command(Action::ReturnToStart); f.tick();
            f.poll({false,false,false,0,true,true});
            f.command(Action::MarkIn); f.command(Action::MarkOut); f.tick();
            check(f.session.snapshot().in==0 && f.session.snapshot().out==180,"animated returns must reject marker commands despite native pause");
            f.finish_return(); f.poll({false,false,false,0,true,true});
            check(f.session.snapshot().out==180,"return completion must require a fresh marker press");
            f.command(Action::Seek,35); f.tick(); f.shortcuts.reset();
            f.poll({false,false,false,0,true,true});
            check(f.session.snapshot().in==0 && f.session.snapshot().out==180,"the first input sample must not invent marker presses");
        }
        {
            Fixture f;
            float previous=f.game.position;
            for(unsigned n=1;n<=24;++n) {
                f.poll({false,false,false,static_cast<std::uint16_t>(n*64)});
                check(f.game.position>previous && f.game.position-previous<.003f,
                      "each small knob sample must move the paused chart smoothly by less than 3 ms instead of accumulating a coarse step");
                previous=f.game.position;
            }
            check(f.game.seeks==0 && f.game.chart_seeks.size()==24,
                  "continuous knob motion must update chart time each frame without repeatedly seeking song audio");
            for(unsigned n=0;n<20;++n) f.poll({false,false,false,1536});
            check(std::abs(f.game.position-(60.f+1536.f*2.f/65536.f))<.0001f && f.game.seeks==1,
                  "stopping the knob must settle at its precise 2-seconds-per-turn target and align song audio once");
        }
        {
            Fixture f; f.command(Action::Resume); f.tick();
            f.poll({true,true,true,0});
            check(f.game.paused && f.game.position==60,"first press must pause at the current position");
            // The next input observation can be delayed while the song is
            // paused. An up sample is not evidence of a continuously held key.
            f.poll({},1500);
            f.poll({true,true,true,0});
            check(!f.game.paused && !f.session.snapshot().rewinding && f.game.position==60 && f.game.seeks==0,
                  "pause, delayed release and second short press must resume at the same position instead of returning to zero");
            f.poll({},1500);
            check(!f.game.paused && !f.session.snapshot().rewinding && f.game.position==60,
                  "delayed release of the resume press must not turn that short press into a return");
            for(unsigned i=0;i<20;++i) {
                f.poll({true,true,true,0}); f.poll({},1500);
                check(f.game.paused==(i%2==0) && !f.session.snapshot().rewinding && f.game.position==60 && f.game.chart_seeks.empty(),
                      "repeated pause/resume presses must keep their position across paused input gaps");
            }
        }
        {
            Fixture f; f.command(Action::Resume); f.tick();
            const auto position=f.game.position;
            f.poll({true,true,true,0});
            check(f.game.paused && f.game.position==position && !f.session.snapshot().rewinding && f.game.seeks==0,
                  "pressing the combination during playback must pause immediately at the current position without a seek");
            f.poll({},100);
            check(f.game.paused && f.game.position==position,"releasing a short pause press must keep the current position paused");
        }
        {
            Fixture f; f.poll({true,true,false,0});
            check(f.game.paused,"partial combination must not toggle playback");
            f.poll({true,true,true,0});
            check(!f.game.paused && f.game.position==60,"press while paused must resume from the current position");
            f.poll({},999);
            check(!f.game.paused,"short release must not toggle a second time");
            f.tap(); check(f.game.paused,"next short release must pause");
            check(f.session.suppress_result() && f.session.native_exit_locked(),"shortcuts must retain native finish and result guards");
        }
        {
            Fixture f; f.command(Action::SetIn,20); f.command(Action::SetOut,40);
            f.command(Action::SetLoop,1); f.command(Action::SetRate,.5f); f.command(Action::Seek,30); f.command(Action::Resume); f.tick();
            f.game.seeks=0;
            f.poll({true,true,true,0}); f.poll({true,true,true,0},1000);
            check(f.session.snapshot().rewinding && f.game.position>0 && f.game.position<30 && f.game.paused && f.game.seeks==0,
                  "hold must begin a visible return instead of teleporting or seeking song audio");
            f.finish_return();
            check(f.game.position==0 && f.game.paused,"animated hold return must finish at absolute zero even with a loop selected");
            check(f.game.chart_seeks.size()>10 && f.game.seeks==1 && f.game.resets==1,"return must show multiple frames and align audio/reset gauge once at zero");
            for(std::size_t i=1;i<f.game.chart_seeks.size();++i)
                check(f.game.chart_seeks[i]<f.game.chart_seeks[i-1],"animated return must move monotonically toward zero");
            const auto seeks=f.game.seeks;
            f.poll({true,true,true,0},3000); f.poll({true,false,true,0});
            f.poll({true,true,true,0},1500); f.poll({});
            check(f.game.seeks==seeks && f.game.paused,"hold must fire once and release must not toggle; require full release to rearm");
            const auto s=f.session.snapshot();
            check(s.loop && s.in==20 && s.out==40 && s.rate==.5f,"return must preserve selection and rate");
        }
        {
            Fixture f; f.poll({true,true,true,0}); f.poll({},1200);
            check(!f.session.snapshot().rewinding && f.game.position==60 && !f.game.paused,
                  "a delayed up sample cannot prove a long press and must not rewind");
            check(f.game.seeks==0 && f.game.chart_seeks.empty(),"delayed release must not move the chart or audio");
        }
        {
            Fixture f; f.command(Action::Reverse); f.tick();
            check(f.game.reversing && !f.session.snapshot().paused,"reverse setup");
            f.poll({true,true,true,0}); f.poll({true,true,true,0},1000);
            check(!f.game.reversing && !f.session.snapshot().reversing && f.session.snapshot().rewinding && f.game.position>0 && f.game.paused,
                  "long press during reverse must stop decoded audio and animate to zero");
            f.finish_return();
            check(f.game.position==0 && f.game.paused,"reverse hold return must finish paused at zero");
        }
        {
            Fixture f; f.poll({false,false,false,16384});
            check(f.game.position>60 && f.game.position<60.5f && f.game.paused,"clockwise paused turn must smoothly advance timeline");
            f.settle_scrub();
            check(std::abs(f.game.position-60.5f)<.0001f,"quarter turn must settle at half a second");
            f.poll({false,false,false,0});
            f.settle_scrub();
            check(std::abs(f.game.position-60.f)<.0001f,"counterclockwise turn must rewind timeline");
            f.shortcuts.reset(); f.poll({false,false,false,65500});
            f.poll({false,false,false,100});
            f.settle_scrub();
            check(std::abs(f.game.position-(60+136*2.f/65536))<.0001f,"encoder wrap must use short positive delta");
            f.poll({false,false,false,65500});
            f.settle_scrub();
            check(std::abs(f.game.position-60.f)<.0001f,"reverse encoder wrap must use short negative delta");
        }
        {
            Fixture f; for(int i=1;i<=8;++i) f.poll({false,false,false,static_cast<std::uint16_t>(i*16)});
            check(f.game.seeks==0 && f.game.position>60 && f.game.chart_seeks.size()==8,"slow sub-detent turns must update every frame without audio seeks");
            f.settle_scrub();
            check(std::abs(f.game.position-(60+128*2.f/65536))<.0001f,"sub-detent input must preserve its precise accumulated distance");
            f.command(Action::Seek,179.99f); f.tick(); f.poll({false,false,false,16512});
            f.settle_scrub();
            check(f.game.position==180 && f.game.paused,"scrubbing must clamp at track end");
            f.command(Action::Seek,.01f); f.tick(); f.poll({false,false,false,128});
            f.settle_scrub();
            check(f.game.position==0,"scrubbing must clamp at zero");
        }
        {
            Fixture f; f.poll({false,false,false,2048}); const auto before=f.game.position;
            f.poll({false,false,false,1984});
            check(f.game.position<before,"reversing the knob must respond immediately without continuing the previous direction");
        }
        {
            Fixture f; f.poll({false,false,false,4096});
            check(f.session.snapshot().scrubbing && f.session.navigation_active(),"knob gesture must keep native visual cleanup in navigation mode");
            const auto visible=f.game.position;
            f.command(Action::Resume); f.tick();
            check(!f.session.snapshot().scrubbing && !f.game.paused && f.game.position==visible && f.game.seeks==1,
                  "Play during smoothing must align audio at the visible position before resuming");
        }
        {
            Fixture f; f.poll({false,false,false,4096});
            f.command(Action::Seek,90); f.tick();
            for(unsigned i=0;i<20;++i) f.poll({false,false,false,4096});
            check(f.game.position==90 && f.game.seeks==1 && !f.session.snapshot().scrubbing,
                  "mouse seek must cancel the knob tail and perform only its own audio seek");
        }
        {
            Fixture f; f.command(Action::Reverse); f.tick(); f.command(Action::Pause); f.tick();
            f.poll({false,false,false,128});
            f.session.tick(f.game.reverse_position,true,f.game,.025f);
            check(f.session.snapshot().position>60 && f.game.reverse_position==60,"paused decoded reverse cursor must not overwrite chart-only smoothing");
            const auto visible=f.session.snapshot().position;
            f.command(Action::Resume); f.tick();
            check(!f.game.reversing && !f.game.paused && f.game.position==visible,
                  "resuming after scrubbing paused reverse audio must not jump to its old cursor");
        }
        {
            Fixture a,b; a.command(Action::ScrubBy,.25f); b.command(Action::ScrubBy,.25f);
            for(unsigned i=0;i<6;++i) a.tick(1.f/60);
            for(unsigned i=0;i<12;++i) b.tick(1.f/120);
            check(std::abs(a.game.position-b.game.position)<.0001f && a.game.seeks==0 && b.game.seeks==0,
                  "smoothing response must match across 60 and 120 Hz");
        }
        {
            Fixture f; f.game.fail_chart=true; f.poll({false,false,false,64});
            check(!f.session.snapshot().ready && !f.session.snapshot().scrubbing && f.game.paused && f.session.suppress_result(),
                  "chart-only scrub failure must pause and retain result protection");
        }
        {
            Fixture f; f.poll({false,false,false,128}); f.game.fail_seek=true;
            f.command(Action::Resume); f.tick();
            check(!f.session.snapshot().ready && f.game.paused && f.session.suppress_result(),
                  "failed audio alignment must block Play and retain result protection");
        }
        {
            Fixture f; f.command(Action::Resume); f.tick();
            f.poll({false,false,false,16384});
            check(f.game.position==60 && !f.game.paused,"playing knob turns must not seek");
            f.command(Action::Pause); f.tick(); f.poll({false,false,false,16384});
            check(f.game.position==60,"pausing must not apply motion accumulated during playback");
        }
        {
            Fixture f; f.command(Action::Disable); f.tick();
            f.poll({true,true,true,16000}); f.poll({},1200);
            check(!f.game.paused && !f.session.snapshot().enabled && f.game.position==60,"disabled training must ignore shortcuts");
            f.command(Action::Enable); f.tick();
            f.poll({true,true,true,0}); f.poll({},1200);
            check(f.session.snapshot().rewinding,"entry animation must ignore shortcuts");
        }
        {
            Fixture f; f.poll({true,true,true,0});
            f.session.end(f.game); f.session.begin({"next.vox",2,3,180.f},true);
            f.command(Action::Enable); f.tick();
            for(int i=0;i<100 && f.session.snapshot().rewinding;++i) f.tick();
            f.poll({true,true,true,20000},1500); f.poll({},1200);
            check(f.game.position==0 && f.game.paused,"held keys and knob baseline must not carry into a new generation");
        }
        {
            Fixture f; f.game.fail_seek=true;
            f.poll({true,true,true,0}); f.poll({true,true,true,0},1000);
            f.finish_return();
            check(!f.session.snapshot().ready && f.game.paused && f.session.suppress_result(),"native seek failure must pause and retain result protection");
        }
        {
            Fixture f; f.game.fail_chart=true;
            f.poll({true,true,true,0}); f.poll({true,true,true,0},1000);
            check(!f.session.snapshot().ready && f.game.paused && f.session.suppress_result(),"native animation failure must pause and retain result protection");
        }
        {
            Fixture f; f.command(Action::TogglePlayback); f.command(Action::TogglePlayback); f.tick();
            check(f.game.paused,"queued toggles must resolve against the latest game-thread state");
        }
        {
            std::vector<unsigned char> device(0x8200);
            auto set=[&](std::size_t at,unsigned value) { std::memcpy(device.data()+at,&value,4); };
            set(0x8168,500); set(0x817c,501); set(0x8180,502); set(0x815c,65000);
            set(0x816c,503); set(0x8178,504);
            std::uintptr_t ptr=reinterpret_cast<std::uintptr_t>(device.data()); GameInput input;
            check(read_game_input(reinterpret_cast<std::uintptr_t>(&ptr),input) && input.start && input.fx_left && input.fx_right && input.vol_left==65000,
                  "native adapter must accept long-held frame counters and the raw encoder");
            check(input.bt_a && input.bt_d,"native adapter must read BT-A and BT-D consecutive-down counters");
            set(0x816c,0); set(0x8178,0); set(0x8170,600); set(0x8174,601);
            check(read_game_input(reinterpret_cast<std::uintptr_t>(&ptr),input) && !input.bt_a && !input.bt_d,
                  "BT-A and BT-D releases must be independent of BT-B and BT-C");
            set(0x817c,0);
            check(read_game_input(reinterpret_cast<std::uintptr_t>(&ptr),input) && !input.fx_left,"native adapter must observe button release");
            ptr=0; check(!read_game_input(reinterpret_cast<std::uintptr_t>(&ptr),input),"missing native device must fail closed");
            check(!read_game_input(0,input),"missing verified slot must fail closed");
            void* inaccessible=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
            check(inaccessible!=nullptr,"allocate inaccessible fixture");
            const bool read=read_game_input(reinterpret_cast<std::uintptr_t>(inaccessible),input);
            VirtualFree(inaccessible,0,MEM_RELEASE);
            check(!read,"native adapter must contain access violations");
        }
        if(argc==2) native_input_conflict(argv[1]);
        std::cout<<"Game input and shortcut scenarios passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
