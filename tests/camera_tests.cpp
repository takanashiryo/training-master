#include "game/camera.hpp"
#include "native/profile.hpp"
#include <windows.h>
#include <array>
#include <cstring>
#include <iostream>
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
            at<float>(space,216+n*4)=static_cast<float>(frame+n)+3.f;
            at<float>(space,240+n*4)=static_cast<float>(frame+n)*.03f;
        }
    }
};
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
        if(module) native_descendant(layout);
        if(module) FreeLibrary(module);
        std::cout<<"240 animated frames, native matrix freeze/restore, seek, re-lock and graph guards passed\n";
        return 0;
    } catch(const std::exception& error) {
        if(module) FreeLibrary(module);
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
