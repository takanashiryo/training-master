#pragma once
#include <cstdint>
namespace training::judgment_filter {
using Ptr=std::uintptr_t;
using Event=Ptr(__fastcall*)(Ptr,Ptr,Ptr,Ptr,float);
using Failed=bool(__fastcall*)(Ptr);
struct Region { bool active{}; float in{},out{}; };
// Returns the untouched original callback result inside the interval. Outside,
// advance the game's note objects but skip the score/gauge callback; return the
// native all-gauges-failed value expected by GameObjectPlayer.
Ptr dispatch(Event original,Ptr context,Ptr output,Ptr note,Ptr result,float judge_time,
             float song_time,Region region,Ptr expected_player,Ptr player_vtable,Failed failed,bool* suppressed=nullptr);
bool allowed(Region region,float song_time,float note_time,bool have_note,
             float press_time,bool have_press);
}
