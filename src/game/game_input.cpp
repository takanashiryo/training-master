#include "game/game_input.hpp"
#include <windows.h>
#include <array>

namespace training {
bool update_game_input_check(GameInputCheckUpdate original,std::uintptr_t checker,
                             std::uintptr_t expected_vtable,bool owned) {
    if(!owned) { original(checker); return true; }
    if(!checker || !expected_vtable || checker>UINTPTR_MAX-24) return false;
    const auto process=GetCurrentProcess();
    std::uintptr_t actual_vtable{};
    if(!ReadProcessMemory(process,reinterpret_cast<const void*>(checker),&actual_vtable,sizeof(actual_vtable),nullptr) ||
       actual_vtable!=expected_vtable) return false;
    // The native player consumes this flag immediately and marks every gauge
    // failed, even before the native hold-to-exit timer expires. Suppress this
    // request at its producer rather than hiding a real gauge failure later.
    const std::uint64_t clear{};
    return WriteProcessMemory(process,reinterpret_cast<void*>(checker+16),&clear,sizeof(clear),nullptr)!=FALSE;
}
bool read_game_input(std::uintptr_t device_slot,GameInput& input) {
    input={};
    if(!device_slot) return false;
    std::uintptr_t device{};
    const auto process=GetCurrentProcess();
    if(!ReadProcessMemory(process,reinterpret_cast<const void*>(device_slot),&device,sizeof(device),nullptr) ||
       !device || device>UINTPTR_MAX-0x8184) return false;
    // Copy the contiguous encoder/button snapshot without faulting on an IO
    // device that has been torn down. No game method or input flag is modified.
    std::array<std::uint32_t,10> values{};
    if(!ReadProcessMemory(process,reinterpret_cast<const void*>(device+0x815c),values.data(),sizeof(values),nullptr)) return false;
    // Buttons are consecutive-down frame counters, not boolean enums.
    input={values[3]!=0,values[8]!=0,values[9]!=0,static_cast<std::uint16_t>(values[0])};
    return true;
}
}
