#include "game/gauge.hpp"
#include "native_fixture.hpp"
#include "native/profile.hpp"
#include "core/session.hpp"
#include <MinHook.h>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <vector>
namespace {
using namespace training;
using namespace training::gauge;
template<class T> T& at(Ptr p,std::size_t o) { return *reinterpret_cast<T*>(p+o); }
template<class T> Ptr ptr(T& t) { return reinterpret_cast<Ptr>(&t); }
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
struct Probe:Backend {
    bool paused{},fail{},fatal{},query_fail{},muted{},seek_fail{},finished{}; float position{}; unsigned resets{},changes{},releases{}; GaugeConfig config;
    bool pause(bool v) override { paused=v; return true; }
    std::vector<float> audio_seeks,chart_seeks;
    bool seek(float v) override { audio_seeks.push_back(v); if(seek_fail) return false; position=v; return true; }
    bool seek_chart(float v) override { check(paused && muted,"failure return must remain silent and paused"); chart_seeks.push_back(v); position=v; return true; }
    bool mute(bool v) override { muted=v; return true; }
    bool rate(float) override { return true; }
    bool finish() override { finished=true; return true; }
    bool gauge(const GaugeConfig& c) override { config=c; ++changes; return !fail; }
    bool reset_gauge() override { ++resets; fatal=false; return !fail; }
    bool gauge_failed(bool& value) override { value=fatal; return !query_fail; }
    void release_gauge() override { ++releases; config.enabled=false; }
};
void session_tests() {
    Session s; s.set_loop_buffers(0,0); Probe b; Track track; track.duration=100; track.gauge_ready=true; s.begin(track,true);
    const auto command=[&](Action a,float value=0.f) { s.submit({s.snapshot().generation,a,value}); s.tick(b.position,b.paused,b); };
    const auto complete=[&] { for(unsigned i=0;i<40 && s.snapshot().rewinding;++i) s.tick(b.position,b.paused,b,.05f); };
    check(!s.snapshot().gauge.enabled,"gauge defaults off");
    b.position=40; command(Action::SetGaugeEnabled,1); check(s.suppress_result() && s.snapshot().enabled && b.config.enabled,"gauge enables protected training");
    complete(); check(b.resets==1 && b.paused && b.position==0,"entry resets blood at the completed return");
    command(Action::SetGaugeType,5); check(b.config.type==GaugeType::MaxxiveArs,"ARS selection reaches backend");
    command(Action::SetGaugeInitial,43.5f); check(b.config.initial==43.5f,"slider updates during drag");
    const auto writes=b.changes; command(Action::SetGaugeInitial,101); command(Action::SetGaugeType,1.5f);
    check(b.changes==writes,"invalid blood/type ignored");
    command(Action::SetIn,10); command(Action::SetOut,20); command(Action::SetLoop,1); command(Action::Resume);
    s.tick(20,false,b,.05f); const auto resets=b.resets; complete();
    check(b.resets==resets+1 && s.snapshot().paused,"default loop resets once after animation and pauses");
    command(Action::SetRepeat,1); command(Action::Resume); s.tick(20,false,b,.05f); complete();
    check(!s.snapshot().paused && b.resets==resets+2,"repeat resets then resumes");
    command(Action::Seek,13); check(b.resets==resets+2,"ordinary seek does not freeze blood");
    command(Action::Restart); check(b.resets==resets+3,"restart resets initial blood");
    command(Action::Seek,0); check(b.resets==resets+4,"return to song start resets blood");
    command(Action::Disable); check(!b.config.enabled && !s.snapshot().gauge.enabled && s.suppress_result(),"disable restores native gauge while retaining taint");
    command(Action::SetGaugeEnabled,1); b.fail=true; command(Action::SetGaugeInitial,55);
    check(!s.snapshot().ready && s.snapshot().paused && s.suppress_result(),"native failure pauses without releasing result guard");
    s.end(b); check(b.releases==1,"teardown releases saved queues");
    s.begin(track,true); check(!s.snapshot().gauge.enabled,"new song defaults off");
}
void recovery_tests() {
    Session s; s.set_loop_buffers(0,0); Probe b; Track track; track.duration=100; track.gauge_ready=true; s.begin(track,true);
    const auto command=[&](Action a,float value=0.f) { s.submit({s.snapshot().generation,a,value}); s.tick(b.position,b.paused,b); };
    const auto complete=[&] {
        for(unsigned i=0;i<100 && s.snapshot().rewinding;++i) {
            check(s.intercept_gauge_failure(b.position,b),"native state must remain held throughout failure return");
            s.tick(b.position,b.paused,b,.025f);
        }
        check(!s.snapshot().rewinding,"failure return completes in bounded time");
    };
    b.fatal=true;
    check(!s.intercept_gauge_failure(40,b) && !s.suppress_result(),"ordinary gameplay failure remains native");
    b.fatal=false; command(Action::Enable);
    for(unsigned i=0;i<100 && s.snapshot().rewinding;++i) s.tick(b.position,b.paused,b,.025f);
    command(Action::Resume); command(Action::SetIn,10); command(Action::SetOut,20);
    check(!s.snapshot().gauge.enabled,"native-mode recovery also works with gauge override off");
    check(!s.intercept_gauge_failure(40,b),"healthy gauge does not alter native play state");
    b.position=40; b.fatal=true; b.audio_seeks.clear(); b.chart_seeks.clear();
    const auto resets=b.resets;
    check(s.intercept_gauge_failure(40,b) && s.snapshot().gauge_returning && s.snapshot().rewinding,"fatal gauge starts return before native failure routing");
    check(b.muted && b.paused && b.audio_seeks.empty(),"failure begins with silent pause, no instant seek");
    s.tick(40,true,b,.025f); const float first=s.snapshot().position;
    check(first>0 && first<40,"first frame visibly moves toward song start");
    check(s.intercept_gauge_failure(first,b),"repeated failure observation keeps return alive");
    s.tick(b.position,true,b,.025f); const float second=s.snapshot().position;
    check(first-second<40-first,"failure return shares cubic ease-out");
    complete();
    check(s.snapshot().position==0 && s.snapshot().paused && b.resets==resets+1 && b.audio_seeks.size()==1,"without Loop return reaches zero, resets once and pauses");
    check(!s.intercept_gauge_failure(0,b),"successful reset releases native state hold");
    command(Action::SetLoop,1); command(Action::Resume); b.position=17; b.fatal=true;
    check(s.intercept_gauge_failure(17,b),"failure before OUT starts loop recovery"); complete();
    check(s.snapshot().position==10 && s.snapshot().paused && !b.fatal,"Loop recovery targets IN and resets blood");
    command(Action::SetRepeat,1); command(Action::Resume); b.position=19; b.fatal=true;
    check(s.intercept_gauge_failure(19,b),"repeat failure starts return"); complete();
    check(s.snapshot().position==10 && !s.snapshot().paused && !b.muted,"Repeat resumes after the same return");
    s.set_loop_buffers(3,7);
    b.position=19; b.fatal=true; check(s.intercept_gauge_failure(19,b),"buffered failure starts return"); complete();
    check(s.snapshot().position==7 && !s.snapshot().paused && !b.fatal,"fatal gauge recovery uses buffered start and resets blood");
    s.set_loop_buffers(0,0);
    b.position=18; b.fatal=true; check(s.intercept_gauge_failure(18,b),"another death can return again");
    command(Action::Disable);
    check(!s.snapshot().gauge_returning && s.intercept_gauge_failure(18,b) && s.native_exit_locked() && s.suppress_result(),
          "transport cleanup must keep fatal native exits guarded until Finish");
    b.fatal=false; command(Action::Enable);
    for(unsigned i=0;i<100 && s.snapshot().rewinding;++i) s.tick(b.position,b.paused,b,.025f);
    command(Action::Resume); b.query_fail=true;
    check(s.intercept_gauge_failure(30,b) && !s.snapshot().ready && s.snapshot().paused,"invalid gauge binding pauses with native failure held");
    command(Action::Finish); check(b.finished && !s.snapshot().gauge_returning,"Finish remains available after recovery fault");
    s.end(b); s.begin(track,true); check(!s.snapshot().gauge_returning,"no recovery flag leaks into next track");
}
struct Deque { Ptr proxy{},map{},size{},head{},count{}; };
struct Vector { Ptr begin{},end{},capacity{}; };
std::unordered_set<void*> allocations;
std::vector<void(__cdecl*)()> exits;
void* __fastcall allocate(std::size_t n) { auto p=std::malloc(n); if(!p) std::abort(); allocations.insert(p); return p; }
void __fastcall free_native(void* p) { if(p) { check(allocations.erase(p)==1,"native free must match allocation"); std::free(p); } }
int __cdecl register_exit(void(__cdecl* p)()) { exits.push_back(p); return 0; }
void hook(Ptr p,void* replacement) {
    void* original{}; check(MH_CreateHook(reinterpret_cast<void*>(p),replacement,&original)==MH_OK,"native fixture hook");
    check(MH_EnableHook(reinterpret_cast<void*>(p))==MH_OK,"native fixture enable");
}
unsigned refreshes{};
Ptr __fastcall refresh(Ptr) { ++refreshes; return 0; }
Ptr __fastcall note_points(Ptr note) { return note+8; }
Ptr gauge_at(Ptr player,std::size_t i) {
    const auto d=at<Deque>(player,88); return at<Ptr>(at<Ptr>(d.map,8*((d.head+i)&(d.size-1))),0);
}
float hp(Ptr p) { return at<float>(at<Ptr>(p,8)-12,4); }
void native_tests(HMODULE module,int fixture) {
    const Ptr image=reinterpret_cast<Ptr>(module); const auto& a=native::addresses();
    const auto address=[&](Ptr old) { return image+native_fixture::rva(fixture,old); };
    check(MH_Initialize()==MH_OK,"MinHook init");
    hook(address(0x7accd4),reinterpret_cast<void*>(allocate));
    hook(address(0x751600),reinterpret_cast<void*>(free_native));
    hook(address(0x7ad1e0),reinterpret_cast<void*>(register_exit));
    Layout l{image+a.world_vtable,image+a.fumen_player_vtable,image+a.fumen_data_vtable,image+a.gauge_ref_vtable,
        reinterpret_cast<Allocate>(image+a.gauge_allocate),reinterpret_cast<Push>(image+a.gauge_type_push),
        reinterpret_cast<Build>(image+a.gauge_build_stack),reinterpret_cast<Destroy>(image+a.gauge_destroy_player),
        reinterpret_cast<Destroy>(image+a.gauge_destroy_types),a.gauge_chart_options?reinterpret_cast<Options>(image+a.gauge_chart_options):nullptr,refresh,reinterpret_cast<Failed>(image+a.gauge_failed),native::world_layout(),native::resolution().chart_gauge_gain};
    const auto build_stack=[&](Ptr p,Ptr types,const std::uint64_t* initial,const std::uint64_t* gain) {
        if(l.chart_gauge_gain) return l.build(p,types,initial,gain);
        using Legacy=Ptr(__fastcall*)(Ptr,Ptr,const std::uint64_t*);
        return reinterpret_cast<Legacy>(l.build)(p,types,initial);
    };
    alignas(8) std::array<unsigned char,1704> world{};
    alignas(8) std::array<unsigned char,208> player{};
    alignas(8) std::array<unsigned char,320> chart{};
    alignas(8) std::array<unsigned char,120> objects{};
    alignas(8) std::array<unsigned char,48> point{};
    alignas(8) std::array<unsigned char,40> note{};
    std::array<Ptr,3> note_vtable{0,0,reinterpret_cast<Ptr>(note_points)};
    at<int>(ptr(point),32)=1; at<unsigned char>(ptr(point),36)=1;
    at<Ptr>(ptr(note),8)=ptr(note_vtable); at<Vector>(ptr(note),16)={ptr(point),ptr(point)+48,ptr(point)+48};
    std::array<std::array<Ptr,2>,200> notes{}; for(auto& n:notes) n={ptr(note),0};
    at<Vector>(ptr(objects),80)={ptr(notes),ptr(notes)+sizeof(notes),ptr(notes)+sizeof(notes)};
    at<Ptr>(ptr(world),0)=l.world_vtable; at<Ptr>(ptr(world),200)=ptr(player); at<Ptr>(ptr(world),152)=ptr(chart);
    at<Ptr>(ptr(player),0)=l.player_vtable; at<Ptr>(ptr(player),24)=ptr(objects); at<Ptr>(ptr(chart),0)=l.chart_vtable;
    const auto init=[&](Ptr address) { auto& d=at<Deque>(address,0); d={}; d.proxy=l.allocate(16); at<Ptr>(d.proxy,0)=address; at<Ptr>(d.proxy,8)=0; };
    init(ptr(player)+88); init(ptr(world)+l.world.gauge_types);
    const std::uint64_t effective=0,empty=0; l.push(ptr(world)+l.world.gauge_types,&effective);
    build_stack(ptr(player),ptr(world)+l.world.gauge_types,&empty,&empty);
    const Ptr original=gauge_at(ptr(player),0); const auto original_count=allocations.size();
    check(hp(original)==0,"native Effective original starts at zero");
    Controller c; c.configure(l); check(c.available(ptr(world)),"actual native gauge must bind");
    bool failed{}; check(c.failed(ptr(world),failed) && !failed,"Effective at zero cannot cause native failure");
    using Current=Ptr(__fastcall*)(Ptr,Ptr);
    const auto selected=[&] {
        std::array<Ptr,2> output{};
        reinterpret_cast<Current>(image+a.gauge_current)(ptr(player),ptr(output));
        const Ptr result=output[0];
        reinterpret_cast<Destroy>(address(0xb8f20))(ptr(output));
        return result;
    };
    using Update=Ptr(__fastcall*)(Ptr,Ptr,const int*,float);
    auto update=reinterpret_cast<Update>(image+a.gauge_update);
    alignas(8) std::array<unsigned char,1704> hud{};
    alignas(8) std::array<unsigned char,272> event{};
    at<Ptr>(ptr(hud),16)=ptr(world)+l.world.music;
    Vector events{ptr(event),ptr(event)+272,ptr(event)+272};
    for(int type=0;type<6;++type) {
        check(c.set(ptr(world),{true,static_cast<GaugeType>(type),64.f}),"native type switch");
        const auto old_refreshes=refreshes;
        check(c.after_events(ptr(hud)+8,ptr(events)) && refreshes==old_refreshes,"unrelated events cannot consume HUD refresh");
        at<unsigned>(ptr(event),0)=12; at<unsigned char>(ptr(event),264)=11;
        check(c.after_events(ptr(hud)+8,ptr(events)) && refreshes==old_refreshes+1,"gauge event refreshes ARS art");
        check(c.after_events(ptr(hud)+8,ptr(events)) && refreshes==old_refreshes+1,"HUD refresh happens once per switch");
        at<unsigned>(ptr(event),0)=0;
        const auto d=at<Deque>(ptr(player),88); const Ptr current=gauge_at(ptr(player),d.count-1);
        check(selected()==current,"native selector starts with requested primary");
        check(hp(current)==64.f,"initial HP reaches primary native model");
        check(at<Ptr>(d.proxy,0)==ptr(player)+88 && at<Ptr>(at<Deque>(ptr(world),l.world.gauge_types).proxy,0)==ptr(world)+l.world.gauge_types,"native proxies follow moved headers");
        const auto count=allocations.size();
        for(unsigned i=0;i<80;++i) check(c.set(ptr(world),{true,static_cast<GaugeType>(type),static_cast<float>(i)}),"live slider reset");
        check(allocations.size()==count,"dragging slider reuses native history capacity");
        check(c.set(ptr(world),{true,static_cast<GaugeType>(type),64.f}),"reset test blood");
        int critical=6; update(current,ptr(point),&critical,1.f);
        // Hard gauges may intentionally have no positive recovery for a judgment.
        check(hp(current)>=64.f,"native recovery is retained");
        int miss=10; check(at<float>(current,84+miss*4)<0,"fixture miss rate is native damage");
        update(current,ptr(point),&miss,2.f); check(hp(current)<64.f,"judgment changes HP rather than locking it");
        if(type<4) {
            for(unsigned n=0;n<400;++n) update(current,ptr(point),&miss,3.f+n);
            check(c.failed(ptr(world),failed) && failed==(type!=0),"native Effective survives zero; hard gauges fail");
            check(c.reset(ptr(world)) && hp(current)==64.f && c.failed(ptr(world),failed) && !failed,"reset clears native fatal state");
        }
        if(type>=4) {
            for(unsigned n=0;n<400 && !at<unsigned char>(at<Ptr>(current,8)-12,9);++n)
                update(current,ptr(point),&miss,3.f+n);
            check(c.failed(ptr(world),failed) && !failed,"ARS downgrade must not be mistaken for fatal failure");
            check(selected()==gauge_at(ptr(player),d.count-2),"native ARS falls back in original order");
            for(unsigned n=0;n<400;++n) for(std::size_t i=0;i<d.count;++i) update(gauge_at(ptr(player),i),ptr(point),&miss,3.f+n);
            check(at<unsigned char>(at<Ptr>(current,8)-12,9)!=0,"ARS primary fails through native damage");
            check(c.reset(ptr(world)) && hp(current)==64 && !at<unsigned char>(at<Ptr>(current,8)-12,9),"practice reset restores failed ARS primary");
            check(selected()==current,"reset reactivates requested ARS primary");
            check(hp(gauge_at(ptr(player),0))==0,"ARS fallback resets to its native initial HP");
        }
    }
    check(!c.set(ptr(world),{true,GaugeType::Count,50}),"bad enum rejected without mutation");
    check(c.release(ptr(world)),"restore original native queues");
    check(gauge_at(ptr(player),0)==original && hp(original)==0 && allocations.size()==original_count,"original gauge and ownership preserved");
    check(c.release(ptr(world)),"release idempotent");
    l.destroy_player(ptr(player)); l.destroy_types(ptr(world)+l.world.gauge_types);
    // Native Excessive, with no overlay gauge replacement, must also recover.
    init(ptr(player)+88); init(ptr(world)+l.world.gauge_types);
    const std::uint64_t excessive=2; l.push(ptr(world)+l.world.gauge_types,&excessive); build_stack(ptr(player),ptr(world)+l.world.gauge_types,&empty,&empty);
    const Ptr native_hard=gauge_at(ptr(player),0); int miss=10;
    for(unsigned i=0;i<400;++i) update(native_hard,ptr(point),&miss,static_cast<float>(i));
    check(c.failed(ptr(world),failed) && failed,"native-mode hard gauge reaches fatal state");
    check(c.reset(ptr(world)) && hp(native_hard)==100 && c.failed(ptr(world),failed) && !failed,"native-mode return restores original initial HP");
    l.destroy_player(ptr(player)); l.destroy_types(ptr(world)+l.world.gauge_types);
    for(auto it=exits.rbegin();it!=exits.rend();++it) (*it)();
    check(allocations.empty(),"all native allocations must be released after repeated switching");
    MH_Uninitialize();
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        session_tests(); recovery_tests();
        if(argc==2) {
            const auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES); check(module!=nullptr,"map reference binary");
            std::string reason; const bool compatible=native::validate(module,reason); check(compatible,reason.c_str());
            const int fixture=native_fixture::select(reason); if(fixture<0) { FreeLibrary(module); return 77; }
            native_tests(module,fixture); FreeLibrary(module);
        }
        std::cout<<"Gauge scenarios passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
