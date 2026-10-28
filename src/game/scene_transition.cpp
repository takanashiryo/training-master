#include "game/scene_transition.hpp"
namespace training {
MusicSelectRoute capture_music_select_route(std::uintptr_t scene) {
    const auto table=*reinterpret_cast<const std::uintptr_t*>(scene);
    const auto info=reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t)>(
        *reinterpret_cast<const std::uintptr_t*>(table+32))(scene);
    if (!info) return {};
    const auto info_table=*reinterpret_cast<const std::uintptr_t*>(info);
    const auto id=reinterpret_cast<unsigned(__fastcall*)(std::uintptr_t)>(
        *reinterpret_cast<const std::uintptr_t*>(info_table))(info);
    const auto state=*reinterpret_cast<const int* const*>(scene+72);
    if (!state) return {};
    return {static_cast<int>(id),state[0],state[1],state[3]};
}
bool request_music_select(std::uintptr_t scene, const MusicSelectRoute& route,NativeNext next) {
    if (!route.valid() || !next) return false;
    auto* state=*reinterpret_cast<int**>(scene+72);
    if (!state || state[0]!=route.parent_id || state[3]!=route.layer) return false;
    // CAppMainFlow::NextState (+0x2fc730), case GAME(43), recognizes argument
    // MUSICSELECT(18) as its native no-result exit. Resume the parent with that
    // argument so its own state advances to 18. A direct sibling jump leaves
    // the parent in state 43; the next selection then incorrectly goes to 16.
    // The supplied trampoline avoids recursing into our RequestNext hook.
    return next(scene,static_cast<std::uintptr_t>(route.process_id));
}
}
