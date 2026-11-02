#include "game/camera.hpp"
#include <windows.h>
#include <cmath>
#include <cstring>

namespace training::camera {
namespace {
template<class T> T& field(Ptr p,std::size_t offset) { return *reinterpret_cast<T*>(p+offset); }
bool bounded(float value) { return std::isfinite(value) && std::fabs(value)<1e6f; }

struct VisualOffsets { std::size_t first,second; };
constexpr VisualOffsets visual_offsets(VisualView kind) {
    switch(kind) {
    case VisualView::Nemsys: return {56,72};
    case VisualView::ButtonBomb: return {256,0};
    case VisualView::KnobBomb: return {464,480};
    case VisualView::KnobBombUpdate: return {472,488};
    }
    return {464,0};
}

template<class Entry,std::size_t N,class Angles>
Entry* capture_source(std::array<Entry,N>& entries,Ptr source,const Angles& angles) {
    for(auto& entry:entries) if(entry.source==source) return &entry;
    for(auto& entry:entries) if(!entry.source) {
        entry={source,angles};
        return &entry;
    }
    return nullptr;
}
}
void NemsysController::reset() { view_=0; active_=false; }
Ptr NemsysController::transform(NemsysTransform original,Ptr args,Ptr output,bool locked,Ptr expected_vtable) {
    if(!locked || !expected_vtable) { reset(); return original(args,output); }
    // The native helper reads two independent rotations from the view and a
    // third horizontal offset from the call arguments. Give it a temporary
    // view with the captured values, without stopping the live animations.
    Ptr view_copy[10]{},call_args[3]{};
    std::uint32_t first[2]{},second[2]{};
    float horizontal{};
    bool valid=false;
    __try {
        if(args) {
            const auto view=field<Ptr>(args,0), x=field<Ptr>(args,8);
            if(view && x && field<Ptr>(view,0)==expected_vtable) {
                const auto a=field<Ptr>(view,72),b=field<Ptr>(view,56);
                if(a && b) {
                    const auto angle_a=field<float>(a,4),angle_b=field<float>(b,4),offset=field<float>(x,0);
                    if(bounded(angle_a) && bounded(angle_b) && bounded(offset)) {
                        if(!active_ || view_!=view) {
                            view_=view; angle_a_=angle_a; angle_b_=angle_b; offset_=offset; active_=true;
                        }
                        std::memcpy(view_copy,reinterpret_cast<const void*>(view),sizeof(view_copy));
                        std::memcpy(first,reinterpret_cast<const void*>(a),sizeof(first));
                        std::memcpy(second,reinterpret_cast<const void*>(b),sizeof(second));
                        std::memcpy(first+1,&angle_a_,sizeof(float));
                        std::memcpy(second+1,&angle_b_,sizeof(float));
                        view_copy[9]=reinterpret_cast<Ptr>(first); view_copy[7]=reinterpret_cast<Ptr>(second);
                        horizontal=offset_;
                        call_args[0]=reinterpret_cast<Ptr>(view_copy);
                        call_args[1]=reinterpret_cast<Ptr>(&horizontal); call_args[2]=field<Ptr>(args,16);
                        valid=call_args[2]!=0;
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { valid=false; }
    if(!valid) { reset(); return original(args,output); }
    return original(reinterpret_cast<Ptr>(call_args),output);
}
void JudgeLineController::reset() { view_=0; active_=false; }
double JudgeLineController::update(JudgeLineUpdate original,Ptr view,bool locked,Ptr expected_vtable) {
    if(!locked || !expected_vtable) { reset(); return original(view); }
    // This view draws both hantei_line and io_front. Borrow only the two input
    // values in a temporary view; leave the real game objects and animations live.
    Ptr view_copy[16]{};
    std::uint32_t first[2]{},second[2]{};
    bool valid=false;
    __try {
        if(view && field<Ptr>(view,0)==expected_vtable) {
            const auto a=field<Ptr>(view,96),b=field<Ptr>(view,112);
            if(a && b) {
                const auto horizontal=field<float>(a,0),angle_a=field<float>(a,4),angle_b=field<float>(b,4);
                if(bounded(horizontal) && bounded(angle_a) && bounded(angle_b)) {
                    if(!active_ || view_!=view) {
                        view_=view; horizontal_=horizontal; angle_a_=angle_a; angle_b_=angle_b; active_=true;
                    }
                    std::memcpy(view_copy,reinterpret_cast<const void*>(view),sizeof(view_copy));
                    std::memcpy(first,reinterpret_cast<const void*>(a),sizeof(first));
                    std::memcpy(second,reinterpret_cast<const void*>(b),sizeof(second));
                    std::memcpy(first,&horizontal_,sizeof(float)); std::memcpy(first+1,&angle_a_,sizeof(float));
                    std::memcpy(second+1,&angle_b_,sizeof(float));
                    view_copy[12]=reinterpret_cast<Ptr>(first); view_copy[14]=reinterpret_cast<Ptr>(second);
                    valid=true;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { valid=false; }
    if(!valid) { reset(); return original(view); }
    return original(reinterpret_cast<Ptr>(view_copy));
}
void CursorController::reset() { view_=0; active_=false; }
Ptr CursorController::dispatch(CursorEvent original,Ptr view,Ptr events,bool locked,Ptr expected_vtable) {
    if(!locked || !expected_vtable) { reset(); return original(view,events); }
    // The event handler uses these three live inputs to rotate laser effects.
    // Substitute a local view so its note events and native objects keep running.
    Ptr view_copy[29]{};
    std::uint32_t first[3]{},second{};
    bool valid=false;
    __try {
        if(view && field<Ptr>(view,0)==expected_vtable) {
            const auto a=field<Ptr>(view,208),b=field<Ptr>(view,224);
            if(a && b) {
                const float x=field<float>(a,4),y=field<float>(a,8),z=field<float>(b,0);
                if(bounded(x) && bounded(y) && bounded(z)) {
                    if(!active_ || view_!=view) { view_=view; angles_={x,y,z}; active_=true; }
                    std::memcpy(view_copy,reinterpret_cast<const void*>(view),sizeof(view_copy));
                    std::memcpy(first,reinterpret_cast<const void*>(a),sizeof(first));
                    std::memcpy(first+1,&angles_[0],sizeof(float));
                    std::memcpy(first+2,&angles_[1],sizeof(float));
                    std::memcpy(&second,&angles_[2],sizeof(float));
                    view_copy[26]=reinterpret_cast<Ptr>(first); view_copy[28]=reinterpret_cast<Ptr>(&second);
                    valid=true;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { valid=false; }
    if(!valid) { reset(); return original(view,events); }
    return original(reinterpret_cast<Ptr>(view_copy),events);
}
bool Controller::before_cursor_event(Ptr world) {
    if(faulted_ || !active_ || borrowed_ || cursor_borrowed_) return false;
    Binding current; Pose pose;
    if(!bind(world,current) || current!=binding_ || !read(current,pose)) return false;
    auto display=frozen_;
    display.position[1][0]=pose.position[1][0];
    if(!write(current,display)) { write(current,pose); faulted_=true; return false; }
    cursor_saved_=pose; cursor_borrowed_=true; return true;
}
bool Controller::after_cursor_event(Ptr world) {
    if(!cursor_borrowed_) return true;
    cursor_borrowed_=false;
    Binding current;
    if(!bind(world,current) || current!=binding_ || !write(current,cursor_saved_)) {
        faulted_=true; active_=false; return false;
    }
    return true;
}
void VisualController::reset() { first_sources_={}; second_sources_={}; }
bool VisualController::begin(Ptr view,Ptr expected_vtable,VisualView kind,VisualLease& lease) {
    lease={};
    if(!view || !expected_vtable) return false;
    const auto [first_offset,second_offset]=visual_offsets(kind);
    __try {
        if(field<Ptr>(view,0)!=expected_vtable) return false;
        const auto first=field<Ptr>(view,first_offset),second=second_offset?field<Ptr>(view,second_offset):0;
        if(!first || (second_offset && !second)) return false;
        const float x=field<float>(first,4),y=field<float>(first,8);
        if(!bounded(x) || !bounded(y)) return false;
        float a{},b{},c{};
        if(second) {
            a=field<float>(second,0); b=field<float>(second,4); c=field<float>(second,16);
            if(!bounded(a) || !bounded(b) || !bounded(c)) return false;
        }
        const auto frozen_first=capture_source(first_sources_,first,std::array<float,2>{x,y});
        if(!frozen_first) return false;
        const auto frozen_second=second?capture_source(second_sources_,second,std::array<float,3>{a,b,c}):nullptr;
        if(second && !frozen_second) return false;
        lease.view=view; lease.first_offset=first_offset; lease.second_offset=second_offset;
        lease.first_original=first; lease.second_original=second;
        std::memcpy(lease.first.data(),reinterpret_cast<const void*>(first),sizeof(lease.first));
        std::memcpy(lease.first.data()+1,&frozen_first->angles[0],sizeof(float));
        std::memcpy(lease.first.data()+2,&frozen_first->angles[1],sizeof(float));
        if(second) {
            std::memcpy(lease.second.data(),reinterpret_cast<const void*>(second),sizeof(lease.second));
            std::memcpy(lease.second.data(),&frozen_second->angles[0],sizeof(float));
            std::memcpy(lease.second.data()+1,&frozen_second->angles[1],sizeof(float));
            std::memcpy(lease.second.data()+4,&frozen_second->angles[2],sizeof(float));
        }
        lease.active=true;
        field<Ptr>(view,first_offset)=reinterpret_cast<Ptr>(lease.first.data());
        if(second) field<Ptr>(view,second_offset)=reinterpret_cast<Ptr>(lease.second.data());
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { end(lease); return false; }
}
bool VisualController::end(VisualLease& lease) {
    if(!lease.active) return true;
    lease.active=false;
    __try {
        bool ok=true;
        if(field<Ptr>(lease.view,lease.first_offset)==reinterpret_cast<Ptr>(lease.first.data()))
            field<Ptr>(lease.view,lease.first_offset)=lease.first_original;
        else ok=false;
        if(lease.second_offset) {
            if(field<Ptr>(lease.view,lease.second_offset)==reinterpret_cast<Ptr>(lease.second.data()))
                field<Ptr>(lease.view,lease.second_offset)=lease.second_original;
            else ok=false;
        }
        return ok;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void Controller::configure(Layout layout) { layout_=layout; reset(); }
void Controller::reset() { binding_={}; frozen_={}; native_={}; cursor_saved_={}; active_=borrowed_=faulted_=cursor_borrowed_=false; }
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
    // CameraUpdater writes the native VOL slam impulse to the second camera
    // space's X position. Preserve that transient feedback while keeping the
    // parent pose, the rest of the child pose and the engine view frozen.
    auto display=frozen_;
    display.position[1][0]=pose.position[1][0];
    if (!write(current,display)) { active_=false; faulted_=true; return false; }
    borrowed_=true;
    return true;
}
}
