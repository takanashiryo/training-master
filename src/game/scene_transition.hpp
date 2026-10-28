#pragma once
#include <cstdint>
namespace training {
using NativeNext=bool(__fastcall*)(std::uintptr_t,std::uintptr_t);
struct MusicSelectRoute {
    int process_id{-1}, parent_id{-1}, next_id{-1}, layer{-1};
    bool valid() const { return process_id>=0 && process_id!=0x7fffffff && parent_id>=0 && layer>=0; }
};
MusicSelectRoute capture_music_select_route(std::uintptr_t scene);
// Called on the game thread after result collection has been suppressed.
bool request_music_select(std::uintptr_t scene, const MusicSelectRoute& route,NativeNext next);
}
