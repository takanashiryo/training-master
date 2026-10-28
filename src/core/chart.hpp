#pragma once
#include "core/musical_timing.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace training {
struct ChartNote { float start{}, end{}; unsigned lane{}; };
struct ChartPoint { float time{}, position{}; unsigned curve{}; bool wide{}; };
struct ChartLaser { unsigned side{}; float width{1}; std::vector<ChartPoint> points; };
struct Chart {
    std::vector<ChartNote> notes;
    std::vector<ChartLaser> lasers;
    std::vector<ChartLine> measures,beats;
    std::vector<ChartTempo> tempos;
    MusicalTimeline timing;
    float first_time{}, last_time{};
    std::string error;
};
// Reader copies bytes on the game thread. The published chart owns every value;
// neither the render thread nor State retains native pointers or CRT containers.
using ChartReader=bool(*)(std::uintptr_t,void*,std::size_t);
std::shared_ptr<const Chart> read_native_chart(std::uintptr_t data,ChartReader read);
}
