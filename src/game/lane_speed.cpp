#include "game/lane_speed.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace training::lane_speed {
namespace {
template<class T> T& field(Ptr p,std::size_t offset) { return *reinterpret_cast<T*>(p+offset); }
thread_local Controller* building{};
struct Segment { float time{},bpm{}; bool stop{}; unsigned char padding[3]{}; float factor{1.f}; };
struct Vector { Ptr begin{},end{},capacity{}; };
static_assert(sizeof(Segment)==16 && sizeof(Vector)==24);
bool positive(float value) { return std::isfinite(value) && value>0.f && value<1e7f; }
}
void Controller::configure(Layout layout) { layout_=layout; binding_={}; active_=borrowed_=false; }
bool Controller::bind(Ptr world,Binding& result,const char** reason) const {
    if(reason) *reason="native functions are unavailable";
    if(!world || !layout_.build || !layout_.load || !layout_.destroy) return false;
    __try {
        Binding b; b.world=world;
        if(reason) *reason="world vtable mismatch";
        if(field<Ptr>(world,0)!=layout_.world_vtable) return false;
        b.context=field<Ptr>(world,168); b.player=field<Ptr>(world,200);
        b.draw=field<Ptr>(world,248); b.chart=field<Ptr>(world,152);
        b.input=field<Ptr>(world,layout_.world.input); b.converter=field<Ptr>(world,layout_.world.converter);
        if(reason) *reason="world object graph or vtable mismatch";
        if(!b.context || !b.player || !b.draw || !b.chart || !b.input || !b.converter ||
           field<Ptr>(b.context,0)!=layout_.context_vtable || field<Ptr>(b.player,0)!=layout_.player_vtable ||
           field<Ptr>(b.draw,0)!=layout_.draw_timer_vtable || field<Ptr>(b.chart,0)!=layout_.fumen_vtable) return false;
        b.params=field<Ptr>(b.context,8); b.objects=field<Ptr>(b.player,24);
        b.calculator=field<Ptr>(b.draw,88);
        if(reason) *reason="player/display timer binding mismatch";
        if(!b.params || !b.objects || !b.calculator || field<Ptr>(b.objects,16)!=b.draw) return false;
        const Ptr range=field<Ptr>(b.draw,72);
        if(reason) *reason="display range/HiSpeed binding mismatch";
        if(!range || field<Ptr>(range,0)!=layout_.range_vtable || field<Ptr>(range,16)!=b.params) return false;
        result=b; return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Controller::read_params(const Binding& b,float* values) const {
    __try {
        std::memcpy(values,reinterpret_cast<void*>(b.params),12);
        return positive(values[0]) && positive(values[1]) && std::isfinite(values[2]) && std::fabs(values[2])<1e7f;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Controller::write_params(const Binding& b,const float* values) const {
    __try { std::memcpy(reinterpret_cast<void*>(b.params),values,12); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Controller::capture_bpm(const Binding& b,float& value) const {
    __try {
        const auto bounds=field<Vector>(b.calculator,0);
        if(!bounds.begin || bounds.end<=bounds.begin || bounds.capacity<bounds.end ||
           (bounds.end-bounds.begin)%20 || (bounds.end-bounds.begin)/20>100000) return false;
        const Ptr offset=field<Ptr>(b.draw,24);
        if(!offset) return false;
        const float time=static_cast<float>(field<std::int64_t>(b.draw,56)*1e-9)+field<float>(offset,0);
        if(!std::isfinite(time)) return false;
        float previous{},first{};
        float last_time=-std::numeric_limits<float>::infinity();
        const bool fixed=field<unsigned char>(b.calculator,24)!=0;
        for(Ptr p=bounds.begin;p<bounds.end;p+=20) {
            const float t=field<float>(p,0),bpm=fixed?100.f:field<float>(p,4)*field<float>(p,12);
            if(!std::isfinite(t) || t<last_time || !std::isfinite(bpm)) return false;
            last_time=t;
            if(!field<unsigned char>(p,8) && positive(bpm)) {
                if(!first) first=bpm;
                if(t<=time) previous=bpm;
            }
        }
        // Enabling inside a stop borrows the preceding moving speed, never zero.
        value=previous?previous:first;
        return positive(value);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Controller::available(Ptr world,const char** reason) const {
    Binding b; float params[3],bpm;
    if(!bind(world,b,reason)) return false;
    if(reason) *reason="invalid HiSpeed display parameters";
    if(!read_params(b,params)) return false;
    if(reason) *reason="invalid or unavailable display timing";
    if(!capture_bpm(b,bpm)) return false;
    if(reason) *reason="ready";
    return true;
}
bool Controller::rebuild(const Binding& b) {
    alignas(16) unsigned char output[120]{};
    bool made=false,ok=false;
    Controller* previous=building;
    building=this;
    __try {
        __try {
            made=layout_.build(b.input,reinterpret_cast<Ptr>(output),b.world+152,b.world+layout_.world.converter,
                               -std::numeric_limits<float>::epsilon())==reinterpret_cast<Ptr>(output);
            if(made) { layout_.load(b.objects,reinterpret_cast<Ptr>(output)); ok=true; }
        } __finally {
            building=previous;
            if(made) layout_.destroy(reinterpret_cast<Ptr>(output));
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok=false; }
    return ok;
}
Ptr Controller::construct_timing(Construct original,Ptr output,Ptr input,bool fixed) {
    if(building!=this || !active_) return original(output,input,fixed);
    // One segment spans negative lead-in, ordinary playback, seeks and loops.
    // Keep the original time origin to avoid unnecessary large-coordinate rounding.
    float start=0.f;
    __try {
        const auto bounds=field<Vector>(input,0);
        if(bounds.begin && bounds.end>bounds.begin) start=std::min(0.f,field<float>(bounds.begin,0));
    } __except(EXCEPTION_EXECUTE_HANDLER) { start=0.f; }
    Segment segment{start,bpm_};
    Vector uniform{reinterpret_cast<Ptr>(&segment),reinterpret_cast<Ptr>(&segment)+sizeof(segment),reinterpret_cast<Ptr>(&segment)+sizeof(segment)};
    return original(output,reinterpret_cast<Ptr>(&uniform),false);
}
bool Controller::before_update(Ptr world) {
    if(!borrowed_) return true;
    Binding current;
    if(!bind(world,current) || current.params!=binding_.params || current.world!=binding_.world) return false;
    if(!write_params(current,native_)) return false;
    borrowed_=false; return true;
}
bool Controller::after_update(Ptr world) {
    if(!active_) return true;
    Binding current;
    if(!bind(world,current) || current.params!=binding_.params || !read_params(current,native_)) return false;
    if(!write_params(current,frozen_)) return false;
    borrowed_=true; return true;
}
bool Controller::matches_updater(Ptr updater,bool event_interface) const {
    if(!active_ || !updater) return false;
    __try { return field<Ptr>(updater,event_interface?8:16)==binding_.params; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Controller::set_locked(Ptr world,bool locked) {
    if(locked==active_) return true;
    Binding b;
    if(!before_update(world) || !bind(world,b)) return false;
    if(locked) {
        if(!read_params(b,frozen_) || !capture_bpm(b,bpm_)) return false;
        std::memcpy(native_,frozen_,sizeof(native_)); binding_=b;
    }
    active_=locked;
    if(!rebuild(b)) return false;
    return after_update(world);
}
void Controller::release(Ptr world) {
    before_update(world); active_=borrowed_=false; binding_={};
}
}
