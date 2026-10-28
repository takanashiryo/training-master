#pragma once
#include "core/gauge_types.hpp"
#include "native/native_layout.hpp"
#include <cstdint>
#include <cstddef>
namespace training::gauge {
using Ptr=std::uintptr_t;
using Allocate=Ptr(__fastcall*)(std::size_t);
using Push=Ptr(__fastcall*)(Ptr,const std::uint64_t*);
using Build=Ptr(__fastcall*)(Ptr,Ptr,const std::uint64_t*,const std::uint64_t*);
using Destroy=void(__fastcall*)(Ptr);
using Refresh=Ptr(__fastcall*)(Ptr);
using Failed=bool(__fastcall*)(Ptr);
using Options=Ptr(__fastcall*)(Ptr,Ptr);
struct Layout {
    Ptr world_vtable{},player_vtable{},chart_vtable{},ref_vtable{};
    Allocate allocate{}; Push push{}; Build build{};
    Destroy destroy_player{},destroy_types{}; Options options{}; Refresh refresh{}; Failed failed{};
    native::WorldLayout world{};
    bool chart_gauge_gain{true};
};
// Owns native queues while the game thread borrows the replacement. Every
// allocation and destruction goes through the game's own container routines.
class Controller {
public:
    void configure(Layout layout) { layout_=layout; }
    bool available(Ptr world) const;
    bool set(Ptr world,GaugeConfig);
    bool reset(Ptr world);
    bool failed(Ptr world,bool& value) const;
    bool release(Ptr world);
    bool after_events(Ptr hud_interface,Ptr events);
private:
    struct Binding { Ptr world{},player{},objects{},chart{}; };
    bool bind(Ptr,Binding&) const;
    bool replace(const Binding&,GaugeConfig);
    bool reset_models(const Binding&);
    Layout layout_{};
    Binding binding_{};
    alignas(8) unsigned char saved_player_[208]{};
    alignas(8) unsigned char saved_types_[40]{};
    Ptr refresh_world_{};
    GaugeConfig config_{};
    bool active_{};
    unsigned char padding_[3]{};
};
}
