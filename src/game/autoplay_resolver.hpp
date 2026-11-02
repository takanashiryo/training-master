#pragma once
#include <windows.h>
#include <cstdint>

namespace training::autoplay_discovery {
struct Layout {
    std::uintptr_t update{}, draw{}, judge{}, judgment_event{}, judgment_gauge{}, judgment_color_option{}, nemsys_vtable{}, nemsys_transform{}, nemsys_update{}, button_bomb_vtable{}, button_bomb_update{}, knob_bomb_vtable{}, knob_bomb_event{}, knob_bomb_primary_vtable{}, knob_bomb_update{}, judge_line_vtable{}, judge_line_update{}, cursor_vtable{}, cursor_event{}, world_vtable{}, context_vtable{}, timer_vtable{};
    std::uint32_t context{}, flags{}, timer{}, time_base{}, time_offset{}, time_enabled{}, s_critical_flag{};
};
struct Result {
    Layout layout{};
    const char* error{};
    explicit operator bool() const { return error == nullptr; }
};
// Read-only discovery. No file identity, known RVA, or instruction patching is involved.
Result resolve(HMODULE module);
}
