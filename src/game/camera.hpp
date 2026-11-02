#pragma once
#include <array>
#include <cstdint>
#include "native/native_layout.hpp"

namespace training::camera {
using Ptr=std::uintptr_t;
// GameWorld dispatches visual events from a separate draw pass. Both passes
// may be nested, so restore the previous context on every exit path.
struct WorldScope {
    Ptr& slot;
    Ptr previous;
    WorldScope(Ptr& value,Ptr world):slot(value),previous(value) { slot=world; }
    ~WorldScope() { slot=previous; }
    WorldScope(const WorldScope&)=delete;
    WorldScope& operator=(const WorldScope&)=delete;
};
inline Ptr visual_world(Ptr updating,Ptr drawing) { return drawing?drawing:updating; }

using Rebuild=void(__fastcall*)(Ptr);
using SetView=void(__fastcall*)(Ptr,const std::int32_t*,float);
struct Layout {
    Ptr world_vtable{},renderer_vtable{},engine_vtable{},camera_world_vtable{},space_vtable{};
    Rebuild rebuild{};
    SetView set_view{};
    native::WorldLayout world{};
};
using NemsysTransform=Ptr(__fastcall*)(Ptr,Ptr);
class NemsysController {
public:
    Ptr transform(NemsysTransform original,Ptr args,Ptr output,bool locked,Ptr expected_vtable);
    void reset();
private:
    Ptr view_{};
    float angle_a_{},angle_b_{},offset_{};
    bool active_{};
};
using JudgeLineUpdate=double(__fastcall*)(Ptr);
class JudgeLineController {
public:
    double update(JudgeLineUpdate original,Ptr view,bool locked,Ptr expected_vtable);
    void reset();
private:
    Ptr view_{};
    float horizontal_{},angle_a_{},angle_b_{};
    bool active_{};
};
using CursorEvent=Ptr(__fastcall*)(Ptr,Ptr);
class CursorController {
public:
    Ptr dispatch(CursorEvent original,Ptr view,Ptr events,bool locked,Ptr expected_vtable);
    void reset();
private:
    Ptr view_{};
    std::array<float,3> angles_{};
    bool active_{};
};
enum class VisualView { Nemsys, ButtonBomb, KnobBomb, KnobBombUpdate };
struct VisualLease {
    Ptr view{},first_original{},second_original{};
    std::size_t first_offset{},second_offset{};
    std::array<std::uint32_t,3> first{};
    std::array<std::uint32_t,5> second{};
    bool active{};
};
class VisualController {
public:
    bool begin(Ptr view,Ptr expected_vtable,VisualView kind,VisualLease& lease);
    bool end(VisualLease& lease);
    void reset();
private:
    struct FirstSource { Ptr source{}; std::array<float,2> angles{}; };
    struct SecondSource { Ptr source{}; std::array<float,3> angles{}; };
    // Different visual components can use different camera input objects and
    // run interleaved; retaining only the last object causes re-capture.
    std::array<FirstSource,8> first_sources_{};
    std::array<SecondSource,8> second_sources_{};
};
// Called only on the game thread. Native camera animation keeps advancing while
// its displayed pose is borrowed; restore it before the next native update.
class Controller {
public:
    void configure(Layout layout);
    bool available(Ptr world) const;
    bool before_update(Ptr world);
    bool after_update(Ptr world,bool locked);
    bool before_cursor_event(Ptr world);
    bool after_cursor_event(Ptr world);
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
    Pose frozen_{},native_{},cursor_saved_{};
    bool active_{},borrowed_{},faulted_{},cursor_borrowed_{};
};
}
