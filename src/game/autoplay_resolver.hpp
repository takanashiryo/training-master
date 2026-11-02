#pragma once
#include <windows.h>
#include <cstdint>

namespace training::autoplay_discovery {
struct Layout {
    std::uintptr_t update{}, judge{}, judgment_event{}, judgment_gauge{}, judgment_color_option{}, world_vtable{}, context_vtable{}, timer_vtable{};
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
