#pragma once
#include <array>
#include <cstdint>
#include "native/native_layout.hpp"

namespace training::camera {
using Ptr=std::uintptr_t;
using Rebuild=void(__fastcall*)(Ptr);
using SetView=void(__fastcall*)(Ptr,const std::int32_t*,float);
struct Layout {
    Ptr world_vtable{},renderer_vtable{},engine_vtable{},camera_world_vtable{},space_vtable{};
    Rebuild rebuild{};
    SetView set_view{};
    native::WorldLayout world{};
};
// Called only on the game thread. Native camera animation keeps advancing while
// its displayed pose is borrowed; restore it before the next native update.
class Controller {
public:
    void configure(Layout layout);
    bool available(Ptr world) const;
    bool before_update(Ptr world);
    bool after_update(Ptr world,bool locked);
    void reset(); // The owner must restore the borrowed pose before destruction.
private:
    struct Binding {
        Ptr world{},renderer{},engine{},camera_world{};
        std::array<Ptr,2> spaces{};
        bool operator==(const Binding&) const=default;
    };
    struct Pose {
        float angle{};
        std::array<std::int32_t,2> center{};
        std::array<std::array<float,3>,2> position{},rotation{};
    };
    bool bind(Ptr world,Binding& result) const;
    bool read(const Binding&,Pose&) const;
    bool write(const Binding&,const Pose&) const;
    Layout layout_{};
    Binding binding_{};
    Pose frozen_{},native_{};
    bool active_{},borrowed_{},faulted_{};
};
}
