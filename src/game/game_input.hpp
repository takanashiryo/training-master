#pragma once
#include "game/shortcuts.hpp"

namespace training {
// The slot and the three button/encoder offsets are verified together by the
// native resolver before this read-only adapter is enabled.
bool read_game_input(std::uintptr_t device_slot,GameInput& input);
using GameInputCheckUpdate=void(__fastcall*)(std::uintptr_t);
// Dispatch the native checker normally outside the owned practice world.
// In that world, neutralize its force-crash request while retaining raw input.
bool update_game_input_check(GameInputCheckUpdate original,std::uintptr_t checker,
                             std::uintptr_t expected_vtable,bool owned);
}
