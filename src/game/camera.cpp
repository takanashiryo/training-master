#include "game/camera.hpp"
#include <windows.h>
#include <cmath>
#include <cstring>

namespace training::camera {
namespace {
template<class T> T& field(Ptr p,std::size_t offset) { return *reinterpret_cast<T*>(p+offset); }
bool bounded(float value) { return std::isfinite(value) && std::fabs(value)<1e6f; }
}
void Controller::configure(Layout layout) { layout_=layout; reset(); }
void Controller::reset() { binding_={}; frozen_={}; native_={}; active_=borrowed_=faulted_=false; }
bool Controller::bind(Ptr world,Binding& result) const {
    if (!world || !layout_.world_vtable || !layout_.rebuild || !layout_.set_view) return false;
    __try {
        if (field<Ptr>(world,0)!=layout_.world_vtable) return false;
        Binding b; b.world=world;
        b.renderer=field<Ptr>(world,layout_.world.renderer);
        if (!b.renderer || field<Ptr>(b.renderer,0)!=layout_.renderer_vtable) return false;
        b.engine=field<Ptr>(b.renderer,24); b.camera_world=field<Ptr>(b.renderer,40);
        if (!b.engine || !b.camera_world || field<Ptr>(b.engine,0)!=layout_.engine_vtable ||
            field<Ptr>(b.camera_world,0)!=layout_.camera_world_vtable) return false;
        b.spaces={field<Ptr>(b.camera_world,8),field<Ptr>(b.camera_world,24)};
        for (const auto space:b.spaces) if (!space || field<Ptr>(space,0)!=layout_.space_vtable) return false;
        // The second camera space inherits the first. Never borrow lane/note spaces.
        if (b.spaces[0]==b.spaces[1] || field<Ptr>(b.spaces[1],8)!=b.spaces[0]) return false;
        result=b; return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Controller::read(const Binding& b,Pose& pose) const {
    __try {
        pose.angle=field<float>(b.engine,424);
        std::memcpy(pose.center.data(),reinterpret_cast<void*>(b.engine+428),sizeof(pose.center));
        for (unsigned i=0;i<b.spaces.size();++i) {
            std::memcpy(pose.position[i].data(),reinterpret_cast<void*>(b.spaces[i]+216),sizeof(pose.position[i]));
            std::memcpy(pose.rotation[i].data(),reinterpret_cast<void*>(b.spaces[i]+240),sizeof(pose.rotation[i]));
            for (unsigned j=0;j<3;++j) if (!bounded(pose.position[i][j]) || !bounded(pose.rotation[i][j])) return false;
        }
        return bounded(pose.angle) && pose.center[0]>-1000000 && pose.center[0]<1000000 &&
            pose.center[1]>-1000000 && pose.center[1]<1000000;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Controller::write(const Binding& b,const Pose& pose) const {
    __try {
        // Parent first: native matrix rebuild reads the parent's composed matrix.
        // Keep scale and all lane/note transforms under native control.
        for (unsigned i=0;i<b.spaces.size();++i) {
            std::memcpy(reinterpret_cast<void*>(b.spaces[i]+216),pose.position[i].data(),sizeof(pose.position[i]));
            std::memcpy(reinterpret_cast<void*>(b.spaces[i]+240),pose.rotation[i].data(),sizeof(pose.rotation[i]));
            layout_.rebuild(b.spaces[i]);
        }
        layout_.set_view(b.engine,pose.center.data(),pose.angle);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Controller::available(Ptr world) const {
    Binding b; Pose pose;
    return !faulted_ && bind(world,b) && read(b,pose);
}
bool Controller::before_update(Ptr world) {
    if (faulted_) return false;
    if (!borrowed_) return true;
    Binding current;
    if (!bind(world,current)) { active_=borrowed_=false; faulted_=true; return false; }
    if (current!=binding_) { active_=borrowed_=false; return true; }
    borrowed_=false;
    if (!write(current,native_)) { active_=false; faulted_=true; return false; }
    return true;
}
bool Controller::after_update(Ptr world,bool locked) {
    if (faulted_) return false;
    if (!locked) { active_=false; return true; }
    Binding current; Pose pose;
    if (!bind(world,current) || !read(current,pose)) { active_=false; faulted_=true; return false; }
    if (!active_ || current!=binding_) { binding_=current; frozen_=pose; active_=true; }
    native_=pose;
    if (!write(current,frozen_)) { active_=false; faulted_=true; return false; }
    borrowed_=true;
    return true;
}
}
