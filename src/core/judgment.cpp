#include "core/judgment.hpp"
#include <cmath>
#include <mutex>
#include <windows.h>
namespace training::judgment {
namespace { std::mutex mutex; Snapshot state; }
void set_available(bool value) { std::lock_guard lock(mutex); state.available=value; }
void set_colors_swapped(bool value) { std::lock_guard lock(mutex); state.colors_swapped=value; }
void set_s_critical_enabled(bool value) { std::lock_guard lock(mutex); state.s_critical_enabled=value; }
void reset() { std::lock_guard lock(mutex); const bool available=state.available,swapped=state.colors_swapped,s_critical=state.s_critical_enabled; state={}; state.available=available; state.colors_swapped=swapped; state.s_critical_enabled=s_critical; }
bool should_reset(bool was_rewinding,bool rewinding,bool did_seek,float target,bool loop,float in,float loop_start) {
    return (was_rewinding && !rewinding) || (!was_rewinding && !rewinding && did_seek &&
        (std::fabs(target)<.001f || (loop && (std::fabs(target-in)<.001f || std::fabs(target-loop_start)<.001f))));
}
void record(unsigned kind,float offset,bool input) {
    if(kind<1 || kind>10) return;
    const unsigned column=kind==1?0:kind==2?1:kind==3?2:
        kind<=6?3:kind==7?4:kind==8?5:6;
    std::lock_guard lock(mutex); ++state.counts[column];
    if(input && kind!=10 && std::isfinite(offset)) {
        const double milliseconds=static_cast<double>(offset)*1000.;
        ++state.timed_hits;
        const auto count=static_cast<double>(state.timed_hits);
        state.average_ms+=(milliseconds-state.average_ms)/count;
        state.absolute_average_ms+=(std::fabs(milliseconds)-state.absolute_average_ms)/count;
    }
}
void observe_native(std::uintptr_t result) {
    __try {
        if(!result) return;
        const auto kind=*reinterpret_cast<const unsigned*>(result);
        const auto delta=*reinterpret_cast<const float*>(result+8);
        const bool input=*reinterpret_cast<const unsigned char*>(result+24)==1 &&
            *reinterpret_cast<const unsigned char*>(result+32)==1 &&
            *reinterpret_cast<const unsigned*>(result+28)==1;
        record(kind,delta,input);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
Snapshot snapshot() { std::lock_guard lock(mutex); return state; }
}
