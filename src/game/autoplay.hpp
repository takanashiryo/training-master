#pragma once
#include "game/autoplay_resolver.hpp"
#include "core/autoplay_mode.hpp"
#include <array>
#include <cstdint>

namespace training::autoplay {
using Update=void(__fastcall*)(std::uintptr_t);
using Judge=std::uintptr_t(__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,float);
using NoteGate=bool(__fastcall*)(std::uintptr_t,std::uintptr_t);
struct Snapshot {
    std::uint64_t updates{},overrides{},faults{},last_tick{};
    std::array<std::uint64_t,11> judgments{};
    float last_error{};
    bool native{},effective{},faulted{};
};
void configure(const autoplay_discovery::Layout& layout);
void begin_track();
// Borrow the native flag for this call only, restoring it even on Windows SEH.
void update(std::uintptr_t world,bool enabled,Update original,AutoplayMode mode=AutoplayMode::All);
// Select the native flag before the caller dispatches and updates this note.
bool note_gate(NoteGate original,std::uintptr_t referee,std::uintptr_t note);
// Native judgment record and return pointer are always preserved unchanged.
std::uintptr_t judge(Judge original,std::uintptr_t referee,std::uintptr_t output,std::uintptr_t note,float time);
Snapshot snapshot();
void clear_counters();
}
