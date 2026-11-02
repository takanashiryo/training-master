#include "game/camera.hpp"
#include "game/post_effect.hpp"
#include <MinHook.h>
#include <vector>
#include "native/profile.hpp"
#include <windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <functional>
#include <limits>
#include <stdexcept>

namespace {
using namespace training::camera;
template<class T> T& at(Ptr p,std::size_t offset) { return *reinterpret_cast<T*>(p+offset); }
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void __fastcall rebuild(Ptr space) {
    // Stand-in matrices observe both pose and parent composition, rather than
    // duplicating the controller's state machine. Native mode uses game code.
    at<float>(space,24)=at<float>(space,216)+at<float>(space,240);
    const Ptr parent=at<Ptr>(space,8);
    at<float>(space,88)=at<float>(space,24)+(parent?at<float>(parent,88):0.f);
}
void __fastcall set_view(Ptr engine,const std::int32_t* center,float angle) {
    at<float>(engine,424)=angle;
    std::memcpy(reinterpret_cast<void*>(engine+428),center,8);
    at<float>(engine,140)=angle;
}
struct Fixture {
    alignas(16) std::array<unsigned char,1600> world{};
    alignas(16) std::array<unsigned char,64> renderer{},camera_world{};
    alignas(16) std::array<unsigned char,560> engine{};
    alignas(16) std::array<unsigned char,272> root{},child{},lane{};
    Ptr w=reinterpret_cast<Ptr>(world.data()),r=reinterpret_cast<Ptr>(renderer.data()),
        e=reinterpret_cast<Ptr>(engine.data()),c=reinterpret_cast<Ptr>(camera_world.data()),
        p=reinterpret_cast<Ptr>(root.data()),q=reinterpret_cast<Ptr>(child.data());
    Layout layout;
    explicit Fixture(Layout value):layout(value) {
        at<Ptr>(w,0)=layout.world_vtable; at<Ptr>(w,layout.world.renderer)=r;
        at<Ptr>(r,0)=layout.renderer_vtable; at<Ptr>(r,24)=e; at<Ptr>(r,40)=c;
        at<Ptr>(e,0)=layout.engine_vtable; at<Ptr>(c,0)=layout.camera_world_vtable;
        at<Ptr>(c,8)=p; at<Ptr>(c,24)=q; at<Ptr>(q,8)=p;
        at<Ptr>(p,0)=at<Ptr>(q,0)=layout.space_vtable;
        for(Ptr space:{p,q}) for(unsigned offset:{228,232,236}) at<float>(space,offset)=1.f;
        at<int>(e,416)=at<int>(e,420)=1024;
        at<int>(e,428)=1024; at<int>(e,432)=-512;
        animate(0); lane.fill(0xa5);
    }
    void animate(unsigned frame) {
        at<float>(w,128)=static_cast<float>(frame); // Substitute native music clock.
        at<float>(e,424)=12.f+static_cast<float>(frame)*.25f;
        for(Ptr space:{p,q}) for(unsigned n=0;n<3;++n) {
            // Native CameraUpdater reserves the child-space X position for
            // transient slam feedback; ordinary follow leaves it untouched.
            if(space!=q || n!=0) at<float>(space,216+n*4)=static_cast<float>(frame+n)+3.f;
            at<float>(space,240+n*4)=static_cast<float>(frame+n)*.03f;
        }
    }
};
struct FakeCurve { Ptr table; float start,end; };
struct FakeLayer { Ptr table; float x{},y{}; };
Ptr __fastcall fake_random_object() { return 1; }
float __fastcall fake_random_value(Ptr) { return .75f; }
float __fastcall fake_curve_value(Ptr,const float*) { return 20.f; }
void* __fastcall fake_curve_start(Ptr p,void* output) {
    float values[]{reinterpret_cast<FakeCurve*>(p)->start,20.f}; std::memcpy(output,values,8); return output;
}
void* __fastcall fake_curve_end(Ptr p,void* output) {
    float values[]{reinterpret_cast<FakeCurve*>(p)->end,0.f}; std::memcpy(output,values,8); return output;
}
void __fastcall fake_layer_offset(Ptr p,float x,float y) { auto* l=reinterpret_cast<FakeLayer*>(p); l->x=x; l->y=y; }
std::vector<unsigned> dispatched;
Ptr __fastcall fake_post_event(Ptr,Ptr v) {
    const auto* a=reinterpret_cast<const Ptr*>(v);
    for(auto p=a[0];p<a[1];p+=272) dispatched.push_back(at<unsigned>(p,0));
    return 1;
}
void post_event_policy() {
    alignas(8) std::array<std::byte,272*5> events{};
    const Ptr p=reinterpret_cast<Ptr>(events.data());
    for(unsigned i=0;i<5;++i) at<unsigned>(p,i*272)=std::array<unsigned,5>{2,40,35,40,5}[i];
    Ptr bounds[]{p,p+events.size(),p+events.size()};
    dispatched.clear(); training::post_effect::dispatch(fake_post_event,0,reinterpret_cast<Ptr>(bounds),false);
    check(dispatched==std::vector<unsigned>({2,40,35,40,5}),"normal playback must preserve every native event");
    dispatched.clear(); training::post_effect::dispatch(fake_post_event,0,reinterpret_cast<Ptr>(bounds),true);
    check(dispatched==std::vector<unsigned>({2,35,5}),"seeking must suppress post-effects but retain other events in order");
}
void native_shake_regression(Ptr image) {
    using namespace training::post_effect;
    const Ptr fn=image+training::native::addresses().posteffect_shake;
    const Ptr rand_object=fn+45+at<std::int32_t>(fn,41);
    const Ptr rand_value=fn+53+at<std::int32_t>(fn,49);
    check(MH_Initialize()==MH_OK,"MinHook native shake test initialization");
    void* unused{};
    check(MH_CreateHook(reinterpret_cast<void*>(rand_object),reinterpret_cast<void*>(fake_random_object),&unused)==MH_OK,"random object hook");
    check(MH_CreateHook(reinterpret_cast<void*>(rand_value),reinterpret_cast<void*>(fake_random_value),&unused)==MH_OK,"random value hook");
    check(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"random substitute enable");
    std::array<Ptr,4> curve_vtable{}; curve_vtable[1]=reinterpret_cast<Ptr>(fake_curve_value);
    curve_vtable[2]=reinterpret_cast<Ptr>(fake_curve_start); curve_vtable[3]=reinterpret_cast<Ptr>(fake_curve_end);
    std::array<Ptr,8> layer_vtable{}; layer_vtable[7]=reinterpret_cast<Ptr>(fake_layer_offset);
    FakeCurve curve{reinterpret_cast<Ptr>(curve_vtable.data()),98.6f,99.1f};
    FakeLayer layer{reinterpret_cast<Ptr>(layer_vtable.data())};
    std::array<Ptr,5> callback{}; callback[1]=reinterpret_cast<Ptr>(&layer); callback[3]=reinterpret_cast<Ptr>(&curve);
    const Ptr self=reinterpret_cast<Ptr>(callback.data()); const auto native=reinterpret_cast<ShakeUpdate>(fn);
    float time=50.f;
    check(!native(self,&time) && (layer.x!=0.f || layer.y!=0.f),"original callback must reproduce persistent shake before its start");
    check(update_shake(native,self,&time,false) && layer.x==0.f && layer.y==0.f,
          "rewinding before the shake start must reset offset and retire the native effect");
    time=98.8f;
    check(!update_shake(native,self,&time,false) && (layer.x!=0.f || layer.y!=0.f),"normal playback must keep the original shake");
    check(update_shake(native,self,&time,true) && layer.x==0.f && layer.y==0.f,"seeking inside an active shake must reset it");
    MH_Uninitialize();
}
bool compute(const Fixture& f) {
    __try {
        f.layout.rebuild(f.p); f.layout.rebuild(f.q);
        f.layout.set_view(f.e,reinterpret_cast<const std::int32_t*>(f.e+428),at<float>(f.e,424));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void run(Layout layout) {
    Fixture f(layout); Controller control; control.configure(layout);
    check(control.available(f.w),"validated camera graph must be available, including negative integer centers");
    check(compute(f),"native camera matrix computation failed");
    const auto frozen_root=f.root,frozen_child=f.child;
    const auto frozen_engine=f.engine;
    const auto unchanged_lane=f.lane;
    check(control.after_update(f.w,true),"initial camera capture failed");
    for(unsigned frame=1;frame<=240;++frame) {
        check(control.before_update(f.w),"native pose restore failed");
        check(at<float>(f.e,424)==12.f+(frame-1)*.25f,"next native update must see its latest evolving angle");
        f.animate(frame); check(compute(f),"animated matrix computation failed");
        check(control.after_update(f.w,true),"camera freeze failed");
        check(at<float>(f.e,424)==12.f && at<int>(f.e,432)==-512,"camera angle and integer center must stay fixed");
        check(std::memcmp(f.root.data()+24,frozen_root.data()+24,192)==0 &&
              std::memcmp(f.child.data()+24,frozen_child.data()+24,192)==0,
              "camera local/composed/inverse matrices must stay fixed, parent before child");
        check(std::memcmp(f.engine.data()+140,frozen_engine.data()+140,192)==0,"engine view matrices must stay fixed");
        check(at<float>(f.w,128)==static_cast<float>(frame) && f.lane==unchanged_lane,
              "camera freeze must not stop clock or write lane/note storage");
    }
    check(control.before_update(f.w) && control.after_update(f.w,false),"unlock failed");
    check(at<float>(f.e,424)==72.f && at<float>(f.p,216)==243.f,
          "unlock must restore current native pose, including after all events have stopped");
    f.animate(250); check(compute(f),"unlocked computation failed");
    check(control.after_update(f.w,true),"re-lock failed");
    check(control.before_update(f.w),"pre-update for seek failed");
    f.animate(10); check(compute(f),"seek camera computation failed");
    check(control.after_update(f.w,true) && at<float>(f.e,424)==74.5f,"seek must preserve the locked displayed pose");
    check(control.before_update(f.w) && at<float>(f.e,424)==14.5f,"native pose must follow seek behind the locked view");
    control.reset(); check(control.after_update(f.w,true),"next track capture failed");
    check(control.before_update(f.w),"cleanup restore failed");
    at<Ptr>(f.q,8)=0;
    check(!control.available(f.w),"changed camera parent graph must be rejected");
    at<Ptr>(f.q,8)=f.p;
    at<float>(f.e,424)=std::numeric_limits<float>::quiet_NaN();
    check(!control.available(f.w),"nonfinite camera pose must be rejected");
    at<Ptr>(f.w,584)=1;
    check(!control.available(f.w),"invalid native pointer must fail without crashing");
}
void slam_shake_regression(Layout layout) {
    Fixture f(layout); Controller control; control.configure(layout);
    check(compute(f) && control.after_update(f.w,true),"camera lock setup for slam shake failed");
    const float root_x=at<float>(f.p,216),angle=at<float>(f.e,424);
    for(float shake:{2.f,-.5f,0.f}) {
        check(control.before_update(f.w),"restore live camera before slam failed");
        f.animate(20); at<float>(f.q,216)=shake;
        check(compute(f) && control.after_update(f.w,true),"slam feedback frame failed");
        check(at<float>(f.q,216)==shake,"locked camera must preserve the native slam shake");
        check(at<float>(f.p,216)==root_x && at<float>(f.e,424)==angle,
              "slam feedback must not re-enable normal camera following");
    }
    check(control.before_update(f.w) && control.after_update(f.w,false),"slam unlock failed");
}
struct NemsysSample { float a,b,x; };
Ptr __fastcall observe_nemsys(Ptr args,Ptr output) {
    const auto view=at<Ptr>(args,0);
    auto& sample=*reinterpret_cast<NemsysSample*>(output);
    sample.a=at<float>(at<Ptr>(view,72),4);
    sample.b=at<float>(at<Ptr>(view,56),4);
    sample.x=at<float>(at<Ptr>(args,8),0);
    return output;
}
void nemsys_regression() {
    alignas(8) std::array<std::byte,80> view{};
    alignas(8) std::array<std::byte,8> a{},b{};
    const Ptr p=reinterpret_cast<Ptr>(view.data()),pa=reinterpret_cast<Ptr>(a.data()),pb=reinterpret_cast<Ptr>(b.data());
    constexpr Ptr vtable=0x1234;
    at<Ptr>(p,0)=vtable; at<Ptr>(p,72)=pa; at<Ptr>(p,56)=pb;
    at<float>(pa,4)=2; at<float>(pb,4)=3; float x=4,y=5;
    Ptr args[]{p,reinterpret_cast<Ptr>(&x),reinterpret_cast<Ptr>(&y)};
    NemsysController controller; NemsysSample sample{};
    const auto call=[&](bool locked) { controller.transform(observe_nemsys,reinterpret_cast<Ptr>(args),reinterpret_cast<Ptr>(&sample),locked,vtable); };
    call(true); at<float>(pa,4)=20; at<float>(pb,4)=30; x=40;
    call(true);
    check(sample.a==2 && sample.b==3 && sample.x==4,"NEMSYS rotation and offset must remain fixed while the camera is locked");
    check(at<float>(pa,4)==20 && at<float>(pb,4)==30 && x==40,"NEMSYS native animation values must remain untouched");
    call(false); check(sample.a==20 && sample.b==30 && sample.x==40,"unlock must restore live NEMSYS motion");
    call(true); check(sample.a==20 && sample.b==30 && sample.x==40,"re-lock must capture the latest position");
    at<Ptr>(p,0)=vtable+1; at<float>(pa,4)=21;
    call(true); check(sample.a==21,"unrecognized view must keep native behavior");
    at<Ptr>(p,0)=vtable; at<float>(pa,4)=22;
    call(true); check(sample.a==22,"reidentified view must capture a fresh pose");
}
NemsysSample judge_line_sample{};
double __fastcall observe_judge_line(Ptr view) {
    const Ptr first=at<Ptr>(view,96),second=at<Ptr>(view,112);
    judge_line_sample={at<float>(second,4),at<float>(first,4),at<float>(first,0)};
    return 42.;
}
void judge_line_regression() {
    alignas(8) std::array<std::byte,128> view{};
    alignas(8) std::array<std::byte,8> a{},b{};
    const Ptr p=reinterpret_cast<Ptr>(view.data()),pa=reinterpret_cast<Ptr>(a.data()),pb=reinterpret_cast<Ptr>(b.data());
    constexpr Ptr vtable=0x9876;
    at<Ptr>(p,0)=vtable; at<Ptr>(p,96)=pa; at<Ptr>(p,112)=pb;
    at<float>(pa,0)=1; at<float>(pa,4)=2; at<float>(pb,4)=3;
    JudgeLineController controller;
    const auto call=[&](bool locked) { return controller.update(observe_judge_line,p,locked,vtable); };
    check(call(true)==42.,"judge-line update return value must pass through");
    at<float>(pa,0)=10; at<float>(pa,4)=20; at<float>(pb,4)=30;
    call(true);
    check(judge_line_sample.a==3 && judge_line_sample.b==2 && judge_line_sample.x==1,
          "judge line and base must remain fixed while camera is locked");
    check(at<float>(pa,0)==10 && at<float>(pa,4)==20 && at<float>(pb,4)==30,
          "freezing the judge line must not stop native angle updates");
    call(false); check(judge_line_sample.a==30 && judge_line_sample.b==20 && judge_line_sample.x==10,
          "unlock must restore the live judge line and base");
    call(true); at<float>(pa,0)=11; call(true); check(judge_line_sample.x==10,"re-lock must capture the latest pose");
    at<Ptr>(p,0)=vtable+1; call(true); check(judge_line_sample.x==11,"unknown view must use native behavior");
}
struct CursorSample { float root,a,b,c; Ptr events; };
CursorSample cursor_sample{};
Ptr cursor_root{};
Ptr __fastcall observe_cursor(Ptr view,Ptr events) {
    const auto a=at<Ptr>(view,208),b=at<Ptr>(view,224);
    cursor_sample={at<float>(cursor_root,240),at<float>(a,4),at<float>(a,8),at<float>(b,0),events};
    return events;
}
void cursor_effect_regression(Layout layout) {
    Fixture f(layout); Controller camera; camera.configure(layout);
    check(compute(f) && camera.after_update(f.w,true),"cursor effect camera setup failed");
    const float frozen_root=at<float>(f.p,240);
    check(camera.before_update(f.w),"cursor event native camera restore failed");
    f.animate(30); check(compute(f),"cursor event live camera setup failed");
    const float live_root=at<float>(f.p,240);
    alignas(8) std::array<std::byte,232> view{};
    alignas(8) std::array<std::byte,12> a{};
    alignas(8) std::array<std::byte,4> b{};
    const Ptr p=reinterpret_cast<Ptr>(view.data()),pa=reinterpret_cast<Ptr>(a.data()),pb=reinterpret_cast<Ptr>(b.data());
    constexpr Ptr table=0x1234, events=0x6789;
    at<Ptr>(p,0)=table; at<Ptr>(p,208)=pa; at<Ptr>(p,224)=pb;
    at<float>(pa,4)=1; at<float>(pa,8)=2; at<float>(pb,0)=3; cursor_root=f.p;
    CursorController cursor;
    auto dispatch=[&](bool locked) {
        const bool borrowed=locked && camera.before_cursor_event(f.w);
        const auto result=cursor.dispatch(observe_cursor,p,events,locked,table);
        if(borrowed) check(camera.after_cursor_event(f.w),"cursor event camera restore failed");
        return result;
    };
    check(dispatch(true)==events,"cursor events and return value must pass through");
    check(cursor_sample.root==frozen_root,"cursor effect must observe the displayed camera angle");
    check(at<float>(f.p,240)==live_root,"cursor event must restore the advancing native camera");
    at<float>(pa,4)=10; at<float>(pa,8)=20; at<float>(pb,0)=30;
    dispatch(true);
    check(cursor_sample.a==1 && cursor_sample.b==2 && cursor_sample.c==3,
          "cursor effect rotation must stay fixed while the inputs continue to move");
    dispatch(false);
    check(cursor_sample.root==live_root && cursor_sample.a==10 && cursor_sample.b==20 && cursor_sample.c==30,
          "unlock must restore native cursor effect direction");
}
void visual_layers_regression() {
    alignas(8) std::array<std::byte,512> nemsys{},button{},knob{};
    std::array<float,3> a{9.f,1.f,2.f}; std::array<float,5> b{3.f,4.f,5.f,6.f,7.f};
    const Ptr np=reinterpret_cast<Ptr>(nemsys.data()),bp=reinterpret_cast<Ptr>(button.data()),kp=reinterpret_cast<Ptr>(knob.data());
    const Ptr ap=reinterpret_cast<Ptr>(a.data()),sp=reinterpret_cast<Ptr>(b.data());
    constexpr Ptr table=0x4545;
    for(const Ptr p:{np,bp,kp}) at<Ptr>(p,0)=table;
    at<Ptr>(np,56)=ap; at<Ptr>(np,72)=sp;
    at<Ptr>(bp,256)=ap; at<Ptr>(kp,464)=ap; at<Ptr>(kp,480)=sp;
    VisualController control; VisualLease lease;
    check(control.begin(np,table,VisualView::Nemsys,lease),"NEMSYS layer angle inputs must be captured");
    check(control.end(lease) && at<Ptr>(np,56)==ap && at<Ptr>(np,72)==sp,"NEMSYS inputs must restore after update");
    a[0]=19.f; a[1]=11.f; a[2]=12.f; b[0]=13.f; b[1]=14.f; b[4]=17.f;
    check(control.begin(np,table,VisualView::Nemsys,lease),"NEMSYS layer inputs must be borrowed");
    const auto na=at<Ptr>(np,56),nb=at<Ptr>(np,72);
    check(at<float>(na,0)==19.f && at<float>(na,4)==1.f && at<float>(na,8)==2.f &&
          at<float>(nb,0)==3.f && at<float>(nb,4)==4.f && at<float>(nb,16)==7.f,
          "NEMSYS knob indicators must retain all camera-controlled layer inputs");
    at<int>(np,24)=42;
    check(control.end(lease) && at<int>(np,24)==42 && a[1]==11.f && b[4]==17.f,
          "NEMSYS timer and native sources must remain live");
    check(control.begin(bp,table,VisualView::ButtonBomb,lease),"button judgment visuals must be borrowed");
    check(at<float>(at<Ptr>(bp,256),4)==1.f && at<float>(at<Ptr>(bp,256),8)==2.f,
          "button judgment effects must use the same locked angles");
    check(control.end(lease) && at<Ptr>(bp,256)==ap,"button effects must restore original sources");
    check(control.begin(kp,table,VisualView::KnobBomb,lease),"knob judgment visuals must be borrowed");
    check(at<float>(at<Ptr>(kp,464),4)==1.f && at<float>(at<Ptr>(kp,480),0)==3.f,
          "knob judgment effects must use the same locked angles");
    check(control.end(lease) && at<Ptr>(kp,464)==ap && at<Ptr>(kp,480)==sp,"knob effects must restore original sources");
    std::array<float,3> alternate{0.f,31.f,32.f};
    at<Ptr>(bp,256)=reinterpret_cast<Ptr>(alternate.data());
    check(control.begin(bp,table,VisualView::ButtonBomb,lease) && control.end(lease),"alternate effect source must be captured");
    check(control.begin(np,table,VisualView::Nemsys,lease),"returning to the NEMSYS source must succeed");
    check(at<float>(at<Ptr>(np,56),4)==1.f && at<float>(at<Ptr>(np,56),8)==2.f,
          "interleaved effect views must not overwrite each other's frozen camera angles");
    check(control.end(lease),"interleaved source restore failed");
    control.reset(); check(control.begin(np,table,VisualView::Nemsys,lease) && at<float>(at<Ptr>(np,56),4)==11.f,
          "re-lock must capture current camera inputs");
    check(control.end(lease),"visual source cleanup failed");
}
// Execute the actual 1013 cursor event handler with isolated graphics stubs.
// This exercises native projection and clip updates, including the two-sided
// 52-byte event ABI, without attaching to or modifying the running game.
struct NativeCursorLayer { Ptr table{}; std::array<std::byte,520> padding{}; int handle{1}; float x{},y{},angle{}; };
int native_frame{},native_inner_frame{};
std::function<void()> native_draw_callback;
void __fastcall native_draw_component(Ptr) { native_draw_callback(); }
void __fastcall native_cursor_visible(Ptr,unsigned) {}
void __fastcall native_cursor_angle(Ptr p,float value) { reinterpret_cast<NativeCursorLayer*>(p)->angle=value; }
void __fastcall native_cursor_position(Ptr p,float x,float y) { auto* layer=reinterpret_cast<NativeCursorLayer*>(p); layer->x=x; layer->y=y; }
void __fastcall native_cursor_size(Ptr,int* x,int* y) { *x=100; *y=100; }
int __fastcall native_cursor_is_valid(int,int) { return 0; }
int __fastcall native_cursor_set_frame(int handle,int,int frame) { (handle==2?native_inner_frame:native_frame)=frame; return 0; }
int __fastcall native_cursor_find(int,const char* name) { return std::strchr(name,'/') ? -1 : 2; }
int __fastcall native_cursor_next(int,int) { return -1; }
int __fastcall native_movie_property(int,int property,int* value) { *value=property==4113?10:0; return 0; }
struct NativeIatPatch {
    Ptr* slot{}; Ptr original{};
    NativeIatPatch(Ptr address,Ptr replacement):slot(reinterpret_cast<Ptr*>(address)),original(*slot) {
        DWORD old{}; check(VirtualProtect(slot,sizeof(Ptr),PAGE_READWRITE,&old)!=0,"cursor replay IAT protection failed");
        *slot=replacement; DWORD ignored{}; VirtualProtect(slot,sizeof(Ptr),old,&ignored);
    }
    ~NativeIatPatch() {
        DWORD old{},ignored{}; if(VirtualProtect(slot,sizeof(Ptr),PAGE_READWRITE,&old)) { *slot=original; VirtualProtect(slot,sizeof(Ptr),old,&ignored); }
    }
};
void native_cursor_indicator(Layout layout,Ptr image) {
    // This fixture is tied to the IDA-audited 1013 callback; other versions
    // still run the portable layer tests and native camera-matrix regression.
    if(training::native::addresses().camera_update!=0x43cac0) return;
    NativeIatPatch valid(image+0x884a20,reinterpret_cast<Ptr>(native_cursor_is_valid));
    NativeIatPatch frame(image+0x884a28,reinterpret_cast<Ptr>(native_cursor_set_frame));
    NativeIatPatch find(image+0x884aa8,reinterpret_cast<Ptr>(native_cursor_find));
    NativeIatPatch next(image+0x884a40,reinterpret_cast<Ptr>(native_cursor_next));
    Fixture f(layout); Controller camera; camera.configure(layout);
    // The real engine constructor provides this projection matrix. The compact
    // fixture does not run that constructor, so initialize a valid identity.
    for(unsigned i=0;i<4;++i) at<float>(f.e,204+i*20)=1.f;
    check(compute(f) && camera.after_update(f.w,true),"native cursor camera capture failed");
    const auto* cursor_table=reinterpret_cast<const Ptr*>(image+0x91f990);
    const auto original=reinterpret_cast<CursorEvent>(cursor_table[1]);
    check(original==reinterpret_cast<CursorEvent>(image+0x42c680),"native cursor replay ABI mismatch");
    // Use the game's real renderer, world, camera matrices and 3D spaces.
    // Only movie-clip output is replaced with recording stubs.
    Ptr proxy[]{0,f.r};
    std::array<Ptr,40> layer_vtable{}; layer_vtable[5]=reinterpret_cast<Ptr>(native_cursor_visible);
    layer_vtable[7]=reinterpret_cast<Ptr>(native_cursor_position); layer_vtable[10]=reinterpret_cast<Ptr>(native_cursor_angle); layer_vtable[39]=reinterpret_cast<Ptr>(native_cursor_size);
    NativeCursorLayer layers[4]; for(auto& layer:layers) layer.table=reinterpret_cast<Ptr>(layer_vtable.data());
    alignas(16) std::array<std::byte,272> middle{},left{},right{};
    const Ptr cursor_parent=reinterpret_cast<Ptr>(middle.data());
    at<Ptr>(cursor_parent,0)=layout.space_vtable; at<Ptr>(cursor_parent,8)=f.q;
    for(unsigned offset:{228u,232u,236u}) at<float>(cursor_parent,offset)=1.f;
    layout.rebuild(cursor_parent);
    Ptr left_space=reinterpret_cast<Ptr>(left.data()),right_space=reinterpret_cast<Ptr>(right.data());
    for(const Ptr space:{left_space,right_space}) {
        at<Ptr>(space,0)=layout.space_vtable; at<Ptr>(space,8)=cursor_parent;
        for(unsigned offset:{228u,232u,236u}) at<float>(space,offset)=1.f;
    }
    alignas(8) std::array<std::byte,256> view{}; const Ptr v=reinterpret_cast<Ptr>(view.data());
    at<Ptr>(v,0)=reinterpret_cast<Ptr>(cursor_table);
    for(unsigned i=0;i<4;++i) at<Ptr>(v,88+i*16)=reinterpret_cast<Ptr>(&layers[i]);
    at<Ptr>(v,152)=left_space; at<Ptr>(v,168)=right_space; at<Ptr>(v,184)=cursor_parent;
    at<Ptr>(v,200)=reinterpret_cast<Ptr>(proxy);
    std::array<float,3> a{0,1,2}; std::array<float,5> b{3,4,5,6,7};
    at<Ptr>(v,208)=reinterpret_cast<Ptr>(a.data()); at<Ptr>(v,224)=reinterpret_cast<Ptr>(b.data());
    alignas(8) std::array<std::byte,52> event{}; const Ptr ep=reinterpret_cast<Ptr>(event.data());
    at<unsigned>(ep,4)=7; at<unsigned>(ep,8)=1; at<float>(ep,12)=.9f; at<float>(ep,16)=1.f;
    at<unsigned char>(ep,20)=1; at<unsigned char>(ep,48)=6;
    Ptr events[]{ep,ep+event.size()}; CursorController cursor;
    auto dispatch=[&](bool locked) {
        const bool borrowed=locked && camera.before_cursor_event(f.w);
        cursor.dispatch(original,v,reinterpret_cast<Ptr>(events),locked,reinterpret_cast<Ptr>(cursor_table));
        if(borrowed) check(camera.after_cursor_event(f.w),"native cursor camera restore failed");
    };
    native_frame=native_inner_frame=-999;
    dispatch(true);
    check(native_frame!=-999 && native_inner_frame!=-999,"native left cursor must reach both animation frames");
    const auto left_initial=layers[0],left_effect=layers[2];
    const auto left_frame=native_frame,left_inner_frame=native_inner_frame;
    at<unsigned>(ep,8)=2; at<float>(ep,12)=.1f;
    native_frame=native_inner_frame=-999; dispatch(true);
    check(native_frame!=-999 && native_inner_frame!=-999,"native right cursor must reach both animation frames");
    const auto right_initial=layers[1],right_effect=layers[3];
    const auto right_frame=native_frame,right_inner_frame=native_inner_frame;
    check(camera.before_update(f.w),"native cursor restore before movement failed");
    f.animate(30); check(compute(f),"native cursor moving camera failed"); layout.rebuild(cursor_parent); a[1]=31; a[2]=32; b[0]=33;
    auto same=[](const NativeCursorLayer& a,const NativeCursorLayer& b) {
        return std::abs(a.x-b.x)<.001f && std::abs(a.y-b.y)<.001f && std::abs(a.angle-b.angle)<.001f;
    };
    dispatch(true);
    check(same(layers[1],right_initial) && same(layers[3],right_effect) &&
          native_frame==right_frame && native_inner_frame==right_inner_frame,
          "native right cursor must stay fixed relative to the locked camera");
    at<unsigned>(ep,8)=1; at<float>(ep,12)=.9f; dispatch(true);
    check(same(layers[0],left_initial) && same(layers[2],left_effect) &&
          native_frame==left_frame && native_inner_frame==left_inner_frame,
          "native left cursor must stay fixed relative to the locked camera");
    // The real world has a separate virtual draw pass. It dispatches the
    // DrawEvent pool after the update pass has already restored its context.
    std::array<Ptr,3> component_vtable{}; component_vtable[2]=reinterpret_cast<Ptr>(native_draw_component);
    Ptr component=reinterpret_cast<Ptr>(component_vtable.data());
    Ptr component_list[]{reinterpret_cast<Ptr>(&component),0};
    at<Ptr>(f.w,440)=reinterpret_cast<Ptr>(component_list);
    at<Ptr>(f.w,448)=reinterpret_cast<Ptr>(component_list+2);
    at<Ptr>(f.w,464)=at<Ptr>(f.w,472)=reinterpret_cast<Ptr>(component_list+2);
    Ptr update_world{},draw_world{};
    native_draw_callback=[&] { dispatch(visual_world(update_world,draw_world)==f.w); };
    const auto draw=reinterpret_cast<void(__fastcall*)(Ptr)>(image+0x4bdb20);
    {
        WorldScope scope(draw_world,f.w);
        draw(f.w);
        check(same(layers[0],left_initial) && native_frame==left_frame && native_inner_frame==left_inner_frame,
              "native draw-stage cursor must retain the active world and locked camera");
        { WorldScope nested(draw_world,123); check(visual_world(0,draw_world)==123,"nested draw context must take precedence"); }
        check(draw_world==f.w,"nested draw context must restore the outer world");
    }
    check(draw_world==0,"draw context must be cleared after the draw pass");
    draw(f.w);
    check(!same(layers[0],left_initial) || native_frame!=left_frame || native_inner_frame!=left_inner_frame,
          "without a draw-stage world the native cursor must follow the live camera");
    native_draw_callback={};
    check(camera.after_update(f.w,false),"native cursor unlock failed");
}
// Replay an active VOL hit effect through the game's per-frame updater.
// The effect is created by the secondary event interface but is animated by
// the primary interface, which must use the same frozen camera inputs.
void native_knob_bomb_lifetime(Layout layout,Ptr image) {
    if(training::native::addresses().camera_update!=0x43cac0) return;
    NativeIatPatch property(image+0x884a98,reinterpret_cast<Ptr>(native_movie_property));
    Fixture f(layout);
    for(unsigned i=0;i<4;++i) at<float>(f.e,204+i*20)=1.f;
    check(compute(f),"native VOL effect camera initialization failed");
    std::array<Ptr,40> layer_vtable{};
    layer_vtable[5]=reinterpret_cast<Ptr>(native_cursor_visible);
    layer_vtable[7]=reinterpret_cast<Ptr>(native_cursor_position);
    layer_vtable[10]=reinterpret_cast<Ptr>(native_cursor_angle);
    NativeCursorLayer layer; layer.table=reinterpret_cast<Ptr>(layer_vtable.data());
    Ptr proxy[]{0,f.r};
    std::array<float,3> first{0,1,2}; std::array<float,5> second{3,4,5,6,7};
    alignas(16) std::array<std::byte,560> storage{};
    const Ptr view=reinterpret_cast<Ptr>(storage.data());
    const Ptr primary=image+0x91f568,secondary=image+0x91f588;
    at<Ptr>(view,0)=primary; at<Ptr>(view,8)=secondary;
    at<Ptr>(view,464)=reinterpret_cast<Ptr>(proxy);
    at<Ptr>(view,472)=reinterpret_cast<Ptr>(first.data());
    at<Ptr>(view,488)=reinterpret_cast<Ptr>(second.data());
    std::array<Ptr,5> item{f.q,0,0,0,reinterpret_cast<Ptr>(&layer)};
    std::array<Ptr,1> block{reinterpret_cast<Ptr>(item.data())};
    std::array<Ptr,3> deque{0,reinterpret_cast<Ptr>(block.data()),1};
    Ptr map=reinterpret_cast<Ptr>(deque.data());
    at<Ptr>(view,424)=reinterpret_cast<Ptr>(&map); at<Ptr>(view,448)=0; at<Ptr>(view,456)=1;
    const auto update=reinterpret_cast<Ptr(__fastcall*)(Ptr)>(image+0x424d10);
    check(at<Ptr>(primary,8)==reinterpret_cast<Ptr>(update),"native VOL effect updater ABI mismatch");
    VisualController control; VisualLease lease;
    check(control.begin(view+8,secondary,VisualView::KnobBomb,lease),"VOL effect event context failed");
    check(control.end(lease),"VOL effect event context restore failed");
    auto locked_update=[&] {
        check(control.begin(view,primary,VisualView::KnobBombUpdate,lease),"VOL effect update context unavailable");
        update(view);
        check(control.end(lease),"VOL effect update context restore failed");
    };
    locked_update(); const float initial=layer.angle;
    for(unsigned frame=1;frame<=360;++frame) {
        first[1]=1.f+frame; first[2]=2.f+frame; second[0]=3.f+frame;
        locked_update();
        check(std::fabs(layer.angle-initial)<.001f,"active VOL hit effect follows camera rotation after the event");
    }
    update(view);
    check(std::fabs(layer.angle-initial)>.001f,"unlocked VOL effect must keep using live camera angles");
}
void native_descendant(Layout layout) {
    Fixture f(layout); Controller control; control.configure(layout);
    f.lane.fill(0);
    const Ptr note=reinterpret_cast<Ptr>(f.lane.data());
    at<Ptr>(note,0)=layout.space_vtable; at<Ptr>(note,8)=f.q;
    for(unsigned offset:{228,232,236}) at<float>(note,offset)=1.f;
    at<float>(note,220)=50.f;
    check(compute(f),"native projection setup failed");
    layout.rebuild(note);
    using Matrix=const void*(__fastcall*)(Ptr);
    const auto matrix=reinterpret_cast<Matrix>(at<Ptr>(layout.space_vtable,40));
    std::array<unsigned char,64> baseline{},moved{};
    std::memcpy(baseline.data(),matrix(note),64);
    check(control.after_update(f.w,true) && control.before_update(f.w),"projection lock failed");
    f.animate(60); check(compute(f),"native animated projection failed");
    matrix(note); // Cache an animated camera pose, as native note updates may do.
    check(control.after_update(f.w,true),"projection freeze failed");
    check(std::memcmp(baseline.data(),matrix(note),64)==0,
          "native descendant matrix must invalidate its cached parent after the camera is locked");
    at<float>(note,220)=100.f; layout.rebuild(note);
    std::memcpy(moved.data(),matrix(note),64);
    check(moved!=baseline,"notes must still move along the lane under a locked camera");
    check(control.before_update(f.w) && control.after_update(f.w,false),"projection unlock failed");
    check(std::memcmp(moved.data(),matrix(note),64)!=0,"native note projection must follow the restored live camera");
}
}
int wmain(int argc,wchar_t** argv) {
    HMODULE module{};
    try {
        Layout layout{0x1000,0x2000,0x3000,0x4000,0x5000,rebuild,set_view};
        if(argc==2) {
            module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
            check(module!=nullptr,"native module mapping failed");
            std::string reason; const bool compatible=training::native::validate(module,reason); check(compatible,reason.c_str());
            const Ptr base=reinterpret_cast<Ptr>(module);
            const auto& a=training::native::addresses();
            layout={base+a.world_vtable,base+a.renderer_vtable,base+a.camera_engine_vtable,
                base+a.camera_world_vtable,base+a.camera_space_vtable,
                reinterpret_cast<Rebuild>(base+a.camera_rebuild_space),reinterpret_cast<SetView>(base+a.camera_set_view),training::native::world_layout()};
        }
        run(layout);
        slam_shake_regression(layout);
        post_event_policy();
        nemsys_regression();
        judge_line_regression();
        cursor_effect_regression(layout);
        visual_layers_regression();
        if(module) { native_descendant(layout); native_shake_regression(reinterpret_cast<Ptr>(module)); native_cursor_indicator(layout,reinterpret_cast<Ptr>(module)); native_knob_bomb_lifetime(layout,reinterpret_cast<Ptr>(module)); }
        if(module) FreeLibrary(module);
        std::cout<<"240 animated frames, native matrix freeze/restore, seek, re-lock and graph guards passed\n";
        return 0;
    } catch(const std::exception& error) {
        if(module) FreeLibrary(module);
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
