#include "game/game_policy.hpp"
#include "native/profile.hpp"
#include <windows.h>
namespace training {
namespace {
template<class T> T& at(std::uintptr_t p,std::size_t offset) { return *reinterpret_cast<T*>(p+offset); }
}
TrainingAccess training_world_access(std::uintptr_t image,std::uintptr_t world) {
    __try {
        if(!image || !training_mode_allowed(at<int>(image,native::addresses().game_mode)))
            return TrainingAccess::UnsupportedMode;
        // Title/attract demos can call the shared world updater. Reject their
        // distinct world type as well as the explicit GameWorld demo flag.
        if(!world || at<std::uintptr_t>(world,0)!=image+native::addresses().world_vtable ||
           at<std::uint8_t>(world,native::world_layout().demo)) return TrainingAccess::Demo;
        return TrainingAccess::Allowed;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return TrainingAccess::Demo; }
}
bool supports_training_world(std::uintptr_t image,std::uintptr_t world) {
    if(training_world_access(image,world)!=TrainingAccess::Allowed) return false;
    // Premium Time's no-fail flag is not by itself a multiplayer marker.
    const bool premium=at<int>(image,native::addresses().game_mode)==6;
    const auto& w=native::world_layout();
    if (at<std::uint8_t>(world,w.special) ||
        (at<std::uint8_t>(world,w.no_fail) && !premium) || at<std::uint8_t>(world,w.battle)) return false;
    const auto matching=at<std::uintptr_t>(image,native::addresses().matching);
    return !matching || at<int>(matching,632)==0;
}
bool prepare_training_finish(std::uintptr_t image,std::uintptr_t world) {
    __try {
        const auto& a=native::addresses();
        if(!world || !a.quiet_exit_state || at<std::uintptr_t>(world,0)!=image+a.world_vtable) return false;
        // The native World Update commits this pending state after Timer Update.
        // Queue its final cleanup, bypassing the presentation-only state and the
        // play-state branch that emits Track Crash/Complete banners and sounds.
        // Keep native matching teardown, finished signaling and scene routing.
        at<std::uintptr_t>(world,80)=0;
        at<int>(world,100)=0;
        at<std::uint8_t>(world,104)=1;
        at<std::uint8_t>(world,native::world_layout().forced_end)=1;
        at<std::uintptr_t>(world,72)=image+a.quiet_exit_state;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool suppress_native_finish(std::uintptr_t image,std::uintptr_t world,bool exit_locked) {
    if(!exit_locked) return false;
    __try {
        if(!world || at<std::uintptr_t>(world,0)!=image+native::addresses().world_vtable) return false;
        auto& flag=at<std::uint8_t>(world,native::world_layout().forced_end);
        if(!flag) return false;
        flag=0; return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool native_track_complete(std::uintptr_t image,std::uintptr_t world,bool& complete) {
    complete=false;
    __try {
        const auto& a=native::addresses();
        if(!world || at<std::uintptr_t>(world,0)!=image+a.world_vtable) return false;
        const auto player=at<std::uintptr_t>(world,200);
        if(!player || at<std::uintptr_t>(player,0)!=image+a.fumen_player_vtable || !at<std::uintptr_t>(player,24)) return false;
        using Finished=bool(__fastcall*)(std::uintptr_t);
        complete=reinterpret_cast<Finished>(image+a.player_finished)(player+8);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
