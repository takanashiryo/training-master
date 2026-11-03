#pragma once
#include <cstdint>
#include "native/native_layout.hpp"

namespace training::lane_speed {
using Ptr=std::uintptr_t;
using Build=Ptr(__fastcall*)(Ptr,Ptr,Ptr,Ptr,float);
using Load=Ptr(__fastcall*)(Ptr,Ptr);
using Destroy=void(__fastcall*)(Ptr);
using Construct=Ptr(__fastcall*)(Ptr,Ptr,bool);
struct Layout {
    Ptr world_vtable{},context_vtable{},player_vtable{},draw_timer_vtable{},fumen_vtable{},range_vtable{};
    Build build{}; Load load{}; Destroy destroy{};
    native::WorldLayout world{};
};
// Game-thread owner. The chart's raw timing/notes are never modified. A native
// rebuild owns its new display objects, including the uniform timing calculator.
// Lock fixes the chart timing while manual HiSpeed changes remain live. Native
// BPM/scroll changes continue behind the display and are restored on unlock.
class Controller {
public:
    void configure(Layout);
    bool available(Ptr world,const char** reason=nullptr) const;
    bool set_locked(Ptr world,bool locked);
    bool before_update(Ptr world);
    bool after_update(Ptr world);
    bool matches_updater(Ptr updater,bool event_interface) const;
    Ptr construct_timing(Construct original,Ptr output,Ptr input,bool fixed);
    void release(Ptr world); // World teardown: restore params without rebuilding dying notes.
private:
    struct Binding {
        Ptr world{},context{},params{},player{},objects{},draw{},calculator{},chart{},input{},converter{};
        bool operator==(const Binding&) const=default;
    };
    bool bind(Ptr world,Binding&,const char** reason=nullptr) const;
    bool read_params(const Binding&,float*) const;
    bool write_params(const Binding&,const float*) const;
    bool rebuild(const Binding&);
    Layout layout_{};
    Binding binding_{};
    float display_[3]{},native_[3]{},bpm_{};
    bool active_{},borrowed_{};
};
}
