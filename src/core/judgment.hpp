#pragma once
#include <array>
#include <cstdint>
namespace training::judgment {
struct Snapshot { std::array<std::uint64_t,7> counts{}; double average_ms{},absolute_average_ms{}; std::uint64_t timed_hits{}; bool available{},colors_swapped{},s_critical_enabled{true}; };
void set_available(bool available);
void set_colors_swapped(bool swapped);
void set_s_critical_enabled(bool enabled);
void reset();
bool should_reset(bool was_rewinding,bool rewinding,bool did_seek,float target,bool loop,float in,float loop_start);
// `kind` is the native JudgeType. Offset is signed seconds, input means a
// real button press, as opposed to a hold/laser tick or an automatic miss.
void record(unsigned kind,float offset,bool input);
void observe_native(std::uintptr_t result);
Snapshot snapshot();
}
