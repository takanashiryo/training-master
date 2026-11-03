#include "game/lane_speed.hpp"
#include "native/profile.hpp"
#include <MinHook.h>
#include <windows.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace training::lane_speed;
template<class T> T& at(Ptr p,std::size_t o) { return *reinterpret_cast<T*>(p+o); }
template<class T> Ptr ptr(T& v) { return reinterpret_cast<Ptr>(&v); }
void check(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
struct Segment { float time,bpm; unsigned char stop,padding[3]; float factor; };
struct Point { Segment raw; float distance; };
struct Vector { Ptr begin{},end{},capacity{}; };
struct Calculator { Vector vector; bool fixed{}; };
static_assert(sizeof(Segment)==16 && sizeof(Point)==20);
std::vector<std::vector<Point>> storage;
Ptr __fastcall append_point(Ptr output,Ptr,Ptr value) {
    auto& v=at<Vector>(output,0);
    if(!v.begin) {
        storage.emplace_back(128); v.begin=ptr(storage.back()[0]); v.end=v.begin; v.capacity=v.begin+128*sizeof(Point);
    }
    check(v.end+sizeof(Point)<=v.capacity,"test allocation capacity");
    std::memcpy(reinterpret_cast<void*>(v.end),reinterpret_cast<void*>(value),sizeof(Point));
    const Ptr result=v.end; v.end+=sizeof(Point); return result;
}
Ptr __fastcall construct(Ptr output,Ptr input,bool fixed) {
    at<Calculator>(output,0)={}; auto& out=at<Calculator>(output,0); out.fixed=fixed;
    const auto in=at<Vector>(input,0); float distance{}; Segment previous{}; bool first=true;
    for(Ptr p=in.begin;p<in.end;p+=sizeof(Segment)) {
        const auto next=at<Segment>(p,0);
        if(!first && !previous.stop) distance+=(next.time-previous.time)*(fixed?100.f:previous.bpm*previous.factor)*8.f;
        Point point{next,distance}; append_point(output,0,ptr(point)); previous=next; first=false;
    }
    return output;
}
Construct calculator_constructor=construct;
Ptr native_player_primary{},native_player_interface{};
// Replay the first compiled vtable initializer independently of the configured
// expected vtable. Forging both from Layout would hide primary/interface mistakes.
Ptr constructor_primary(HMODULE module) {
    using namespace training::native;
    const Ptr base=reinterpret_cast<Ptr>(module);
    const auto& nt=at<IMAGE_NT_HEADERS64>(base,at<IMAGE_DOS_HEADER>(base,0).e_lfanew);
    const auto* sections=IMAGE_FIRST_SECTION(&nt);
    for(const auto& anchor:families().front().anchors) if(std::string_view(anchor.name)=="lane_player_constructor") {
        const std::string_view pattern=anchor.pattern; const auto size=pattern.size()/2;
        std::vector<int> bytes; bytes.reserve(size);
        for(std::size_t i=0;i<pattern.size();i+=2)
            bytes.push_back(pattern.substr(i,2)=="??"?-1:std::stoi(std::string(pattern.substr(i,2)),nullptr,16));
        Ptr found{};
        for(unsigned section=0;section<nt.FileHeader.NumberOfSections;++section) {
            const auto& s=sections[section]; if(!(s.Characteristics&IMAGE_SCN_MEM_EXECUTE) || s.Misc.VirtualSize<size) continue;
            for(std::size_t offset=0;offset<=s.Misc.VirtualSize-size;++offset) {
                const Ptr candidate=base+s.VirtualAddress+offset; bool same=true;
                for(std::size_t i=0;i<size;++i) if(bytes[i]>=0 && at<unsigned char>(candidate,i)!=bytes[i]) { same=false; break; }
                if(same) { check(!found,"player constructor must resolve uniquely"); found=candidate; }
            }
        }
        check(found!=0,"native player constructor absent");
        const auto& ref=anchor.references.front();
        check(ref.access==Access::ReadOnly && ref.kind==ReferenceKind::Relative32,"expected primary vtable initializer");
        const auto& interface_ref=anchor.references[1];
        native_player_interface=found+interface_ref.next+at<std::int32_t>(found,interface_ref.operand);
        return found+ref.next+at<std::int32_t>(found,ref.operand);
    }
    throw std::runtime_error("player constructor ABI guard absent");
}
float evaluate(const Calculator& c,float time) {
    const Point* found{};
    for(Ptr p=c.vector.begin;p<c.vector.end;p+=sizeof(Point)) {
        const auto* point=reinterpret_cast<const Point*>(p); if(point->raw.time>time) break; found=point;
    }
    if(!found) return 0;
    return found->distance+(found->raw.stop?0.f:(time-found->raw.time)*(c.fixed?100.f:found->raw.bpm*found->raw.factor)*8.f);
}
struct Fixture;
Fixture* active{};
Ptr __fastcall build(Ptr,Ptr output,Ptr,Ptr,float);
Ptr __fastcall load(Ptr,Ptr);
void __fastcall destroy(Ptr);
struct Fixture {
    alignas(8) std::array<unsigned char,1704> world{};
    alignas(8) std::array<unsigned char,400> context{},player{},objects{},draw{},chart{},range{};
    std::array<float,6> params{3.f,600.f,600.f,123.f,1.f,2.f};
    std::array<Segment,4> segments{{{0,100,0,{},1},{5,200,0,{},1},{10,200,1,{},1},{12,80,0,{},.5f}}};
    float offset{};
    std::array<Calculator,8> maps{};
    unsigned builds{},loads{},destroys{};
    bool fail_build{};
    Layout layout; Controller controller;
    Ptr w=ptr(world),d=ptr(draw),p=ptr(params);
    explicit Fixture(Layout l):layout(l) {
        active=this; controller.configure(layout);
        at<Ptr>(w,0)=l.world_vtable; at<Ptr>(w,168)=ptr(context); at<Ptr>(w,200)=ptr(player);
        at<Ptr>(w,248)=d; at<Ptr>(w,152)=ptr(chart); at<Ptr>(w,layout.world.input)=ptr(world[1600]); at<Ptr>(w,layout.world.converter)=ptr(world[1616]);
        at<Ptr>(ptr(context),0)=l.context_vtable; at<Ptr>(ptr(context),8)=p;
        at<Ptr>(ptr(player),0)=l.player_vtable; at<Ptr>(ptr(player),24)=ptr(objects);
        if(native_player_primary) at<Ptr>(ptr(player),0)=native_player_primary;
        at<Ptr>(ptr(objects),16)=d; at<Ptr>(d,0)=l.draw_timer_vtable; at<Ptr>(ptr(chart),0)=l.fumen_vtable;
        at<Ptr>(d,24)=ptr(offset); at<Ptr>(d,72)=ptr(range);
        at<Ptr>(ptr(range),0)=l.range_vtable; at<Ptr>(ptr(range),16)=p;
        auto input=Vector{ptr(segments[0]),ptr(segments[0])+sizeof(segments),ptr(segments[0])+sizeof(segments)};
        calculator_constructor(ptr(maps[0]),ptr(input),false); at<Ptr>(d,88)=ptr(maps[0]); time(8);
    }
    void time(float t) { at<std::int64_t>(d,56)=static_cast<std::int64_t>(t*1e9); }
    Calculator& map() { return at<Calculator>(at<Ptr>(d,88),0); }
};
Ptr __fastcall build(Ptr,Ptr output,Ptr chart,Ptr converter,float from) {
    auto& f=*active; check(chart==f.w+152 && converter==f.w+f.layout.world.converter && from<0,"native rebuild inputs must retain original chart/converter");
    if(f.fail_build) return 0;
    ++f.builds; check(f.builds<f.maps.size(),"fixture map capacity");
    auto input=Vector{ptr(f.segments[0]),ptr(f.segments[0])+sizeof(f.segments),ptr(f.segments[0])+sizeof(f.segments)};
    f.controller.construct_timing(calculator_constructor,ptr(f.maps[f.builds]),ptr(input),false);
    at<Ptr>(output,96)=ptr(f.maps[f.builds]); return output;
}
Ptr __fastcall load(Ptr objects,Ptr output) {
    auto& f=*active; check(objects==ptr(f.objects),"native display load must target the current player");
    at<Ptr>(f.d,88)=at<Ptr>(output,96); ++f.loads; return objects;
}
void __fastcall destroy(Ptr) { ++active->destroys; }
float drawn_position{};
void __fastcall render_sink(Ptr,Ptr event) { drawn_position=at<float>(event,12); }
bool native_note(Ptr method,Ptr note,Ptr values) {
    __try { reinterpret_cast<void(__fastcall*)(Ptr,Ptr)>(method)(note,values); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void run(Layout layout,Ptr range_method=0,Ptr button_method=0) {
    Fixture f(layout); const auto raw=f.segments;
    const float reference_bpm=f.params[1]/f.params[0];
    const char* reason{};
    check(f.controller.available(f.w,&reason) && std::string_view(reason)=="ready","valid speed graph must be available");
    if(native_player_interface) {
        at<Ptr>(ptr(f.player),0)=native_player_interface;
        check(!f.controller.available(f.w,&reason) && std::string_view(reason)=="world object graph or vtable mismatch",
              "secondary interface must not pass as the primary player object");
        at<Ptr>(ptr(f.player),0)=native_player_primary;
        check(f.controller.available(f.w,&reason),"restoring the actual primary player must re-enable availability");
    }
    check(f.controller.set_locked(f.w,true),"lock/rebuild failed");
    check(f.builds==1 && f.loads==1 && f.destroys==1 && f.map().vector.end-f.map().vector.begin==sizeof(Point),
          "native rebuilt display must own one uniform timing segment and destroy the temporary result");
    Calculator unrelated;
    auto input=Vector{ptr(f.segments[0]),ptr(f.segments[0])+sizeof(f.segments),ptr(f.segments[0])+sizeof(f.segments)};
    f.controller.construct_timing(calculator_constructor,ptr(unrelated),ptr(input),true);
    check(unrelated.fixed && unrelated.vector.end-unrelated.vector.begin==4*sizeof(Point),
          "other worlds and unrelated constructors must retain their original segments and flags");
    for(float time:{1.f,6.f,10.5f,15.f,120.f}) {
        check(f.controller.before_update(f.w),"restore native params");
        f.time(time); f.params[0]=1.f+time; f.params[1]=200.f+time; f.params[2]=100.f+time;
        const auto native=f.params; check(f.controller.after_update(f.w),"fixed params apply");
        check(f.params[0]==native[1]/reference_bpm && f.params[1]==native[1] && f.params[2]==native[1] && f.params[3]==123.f,
              "manual HiSpeed changes must immediately reach the display without changing unrelated values");
        const float a=evaluate(f.map(),time),b=evaluate(f.map(),time+1),c=evaluate(f.map(),time+2);
        check(std::fabs((b-a)-1600.f)<.02f && std::fabs((c-b)-1600.f)<.02f,
              "BT/FX/VOL coordinates must advance uniformly through BPM, scroll-factor and stop boundaries");
        if(range_method) {
            at<float>(ptr(f.range),8)=a; std::array<float,3> values{};
            reinterpret_cast<float*(__fastcall*)(Ptr,float*)>(range_method)(ptr(f.range),values.data());
            const float note_distance=evaluate(f.map(),time+.5f);
            const float normalized=(note_distance-values[1])/(values[2]-values[1]);
            check(std::fabs(normalized-800.f*f.params[0]/3400.f)<.001f,"native DrawRange must use the latest manual HiSpeed multiplier");
            // Native ButtonNote consumes the same mapped coordinate as the long/laser display pipeline.
            alignas(16) std::array<unsigned char,144> note{}; std::array<Ptr,3> sink_vtable{0,0,reinterpret_cast<Ptr>(render_sink)};
            const Ptr sink=ptr(sink_vtable); Ptr sink_object=sink;
            at<Ptr>(ptr(note),64)=ptr(sink_object); at<float>(ptr(note),28)=note_distance; at<int>(ptr(note),128)=1;
            drawn_position=-1;
            check(native_note(button_method,ptr(note),ptr(values)) && std::fabs(drawn_position-normalized)<.001f,
                  "native ButtonNote render must emit the same constant-speed position");
        }
        check(f.controller.before_update(f.w) && f.params==native,"unlock-before-update must preserve evolving native values");
    }
    check(std::memcmp(f.segments.data(),raw.data(),sizeof(raw))==0,"raw chart speed/timing records must remain byte-identical");
    check(f.controller.set_locked(f.w,false) && f.builds==2 && f.loads==2 && f.destroys==2,"unlock must rebuild the original speed map once");
    check(evaluate(f.map(),11)==evaluate(f.map(),10) && evaluate(f.map(),9)-evaluate(f.map(),8)==1600,
          "native BPM and stop behavior must return on unlock");
    f.time(15); check(f.controller.set_locked(f.w,true),"re-lock");
    check(std::fabs((evaluate(f.map(),16)-evaluate(f.map(),15))*f.params[0]-f.params[1]*8.f)<.02f,
          "re-lock must use the latest configured speed instead of the current BPM times scroll factor");
    std::array<unsigned char,40> updater{}; at<Ptr>(ptr(updater),16)=f.p;
    check(f.controller.matches_updater(ptr(updater),false) && f.controller.matches_updater(ptr(updater)+8,true),"match primary/secondary Hisp interfaces");
    f.controller.release(f.w); check(f.builds==3 && !f.controller.matches_updater(ptr(updater),false),"teardown must restore ownership without rebuilding dying notes");
    Fixture stopped(layout); stopped.time(11);
    check(stopped.controller.set_locked(stopped.w,true) && evaluate(stopped.map(),2)==3200,"enabling inside a stop must restore the configured speed");
    stopped.controller.release(stopped.w);
    Fixture broken(layout); broken.fail_build=true;
    check(!broken.controller.set_locked(broken.w,true) && !broken.loads && !broken.destroys,"failed construction must not load or destroy an absent result");
    broken.controller.release(broken.w);
    at<Ptr>(broken.w,168)=1; check(!broken.controller.available(broken.w),"invalid native context must fail safely");
}
void configured_speed_regression(Layout layout) {
    // Native segments include a speedup, stop, and a slow scroll section.
    // Lock must return to HiSpeed 600 in each, including first enabling late.
    for(float time:{1.f,8.f,11.f,15.f}) {
        Fixture f(layout); f.time(time);
        f.params[2]=time<5?300.f:time<10?600.f:time<12?0.f:120.f;
        const auto native=f.params;
        check(f.controller.set_locked(f.w,true),"configured speed lock must work in every song speed phase");
        check(f.params[0]==native[0] && f.params[1]==600.f && f.params[2]==600.f,
              "enabling lock must preserve the user's multiplier/setting and restore the actual speed display");
        const float velocity=(evaluate(f.map(),time+1)-evaluate(f.map(),time))*f.params[0];
        check(std::fabs(velocity-600.f*8.f)<.02f,"locked note movement must match configured HiSpeed 600 after any song speed change");
        check(f.controller.before_update(f.w) && f.params==native,"native speed phase must remain available behind the locked display");
        // Simulate the real HispUpdater input path followed by a song event:
        // manual input changes +0/+4; chart speed changes only update +8.
        f.params[0]=3.5f; f.params[1]=700.f; f.params[2]=native[2]*(700.f/600.f);
        const auto manual=f.params;
        check(f.controller.after_update(f.w) && f.params[0]==3.5f && f.params[1]==700.f && f.params[2]==700.f,
              "manual adjustment from 600 to 700 must immediately update locked speed and HUD");
        const float adjusted_velocity=(evaluate(f.map(),time+1)-evaluate(f.map(),time))*f.params[0];
        check(std::fabs(adjusted_velocity-700.f*8.f)<.02f && f.builds==1,
              "manual adjustment must change actual note movement without rebuilding the timing map");
        check(f.controller.before_update(f.w) && f.params==manual,"native input must retain the latest manual setting");
        f.params[2]=0.f; const auto song_stop=f.params;
        check(f.controller.after_update(f.w) && f.params[0]==3.5f && f.params[2]==700.f,
              "a later automatic song speed change must not override the user's live HiSpeed");
        check(f.controller.set_locked(f.w,false) && f.params==song_stop,"unlock must restore the native phase and retain the manual setting");
        f.controller.release(f.w);
    }
}
}
int wmain(int argc,wchar_t** argv) {
    HMODULE module{};
    try {
        Layout layout{1,2,3,4,5,6,build,load,destroy}; Ptr range_method{},button_method{};
        if(argc==2) {
            module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
            check(module!=nullptr,"native mapping failed"); std::string reason;
            const bool valid=training::native::validate(module,reason); check(valid,reason.c_str());
            const Ptr base=reinterpret_cast<Ptr>(module); const auto& a=training::native::addresses();
            native_player_primary=constructor_primary(module);
            std::cout<<"Native primary player vtable RVA="<<std::hex<<native_player_primary-base
                     <<", configured="<<a.fumen_player_vtable<<std::dec<<'\n';
            calculator_constructor=reinterpret_cast<Construct>(base+a.lane_timing_constructor);
            layout={base+a.world_vtable,base+a.game_context_vtable,base+a.fumen_player_vtable,base+a.draw_timer_vtable,
                base+a.fumen_data_vtable,base+a.lane_range_vtable,build,load,destroy,training::native::world_layout()};
            range_method=at<Ptr>(base+a.lane_range_vtable,8); button_method=at<Ptr>(base+a.lane_button_vtable,40);
            Ptr append{};
            for(const auto& anchor:training::native::families().front().anchors)
                if(anchor.symbol==training::native::Symbol::lane_timing_constructor)
                    for(const auto& ref:anchor.references) if(ref.access==training::native::Access::Code)
                        append=base+a.lane_timing_constructor+ref.next+at<std::int32_t>(base+a.lane_timing_constructor,ref.operand);
            check(append && MH_Initialize()==MH_OK,"native allocation boundary discovery"); void* unused{};
            check(MH_CreateHook(reinterpret_cast<void*>(append),reinterpret_cast<void*>(append_point),&unused)==MH_OK &&
                  MH_EnableHook(reinterpret_cast<void*>(append))==MH_OK,"native allocation substitute hook");
        }
        run(layout,range_method,button_method);
        configured_speed_regression(layout);
        if(module) { MH_Uninitialize(); FreeLibrary(module); }
        std::cout<<"Lane Speed uniform display, native parameter restore, re-lock, stop/scroll, rebuild cleanup and graph guards passed\n";
        return 0;
    } catch(const std::exception& error) {
        if(module) { MH_Uninitialize(); FreeLibrary(module); }
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
