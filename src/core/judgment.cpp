#include "core/judgment.hpp"
#include <cmath>
#include <mutex>
#include <vector>
#include <windows.h>
namespace training::judgment {
namespace {
std::mutex mutex;
Snapshot state;
struct Entry { float position; Snapshot state; };
std::vector<Entry> history;
void restore_locked(float position) {
    while(!history.empty() && history.back().position>position) history.pop_back();
    const bool available=state.available,swapped=state.colors_swapped,s_critical=state.s_critical_enabled;
    state=history.empty()?Snapshot{}:history.back().state;
    state.available=available; state.colors_swapped=swapped; state.s_critical_enabled=s_critical;
}
}
void set_available(bool value) { std::lock_guard lock(mutex); state.available=value; }
void set_colors_swapped(bool value) { std::lock_guard lock(mutex); state.colors_swapped=value; }
void set_s_critical_enabled(bool value) { std::lock_guard lock(mutex); state.s_critical_enabled=value; }
void reset() { std::lock_guard lock(mutex); history.clear(); restore_locked(0.f); }
void restore(float position) {
    if(!std::isfinite(position)) return;
    std::lock_guard lock(mutex); restore_locked(position);
}
void record(unsigned kind,float offset,bool input,float position) {
    if(kind<1 || kind>10 || !std::isfinite(position)) return;
    const unsigned column=kind==1?0:kind==2?1:kind==3?2:
        kind<=6?3:kind==7?4:kind==8?5:6;
    std::lock_guard lock(mutex);
    if(!history.empty() && history.back().position>position) restore_locked(position);
    ++state.counts[column];
    if(input && kind!=10 && std::isfinite(offset)) {
        const double milliseconds=static_cast<double>(offset)*1000.;
        ++state.timed_hits;
        const auto count=static_cast<double>(state.timed_hits);
        state.average_ms+=(milliseconds-state.average_ms)/count;
        state.absolute_average_ms+=(std::fabs(milliseconds)-state.absolute_average_ms)/count;
    }
    history.push_back({position,state});
}
namespace {
// Preserve a separate faulting frame: Clang SEH cannot protect loads in the
// same frame as __try, even though MSVC accepts the original inline form.
__declspec(noinline) void observe_native_unchecked(std::uintptr_t result,float position) {
    if(!result) return;
    const auto kind=*reinterpret_cast<const unsigned*>(result);
    const auto delta=*reinterpret_cast<const float*>(result+8);
    const bool input=*reinterpret_cast<const unsigned char*>(result+24)==1 &&
        *reinterpret_cast<const unsigned char*>(result+32)==1 &&
        *reinterpret_cast<const unsigned*>(result+28)==1;
    record(kind,delta,input,position);
}
}
void observe_native(std::uintptr_t result,float position) {
    __try { observe_native_unchecked(result,position); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
Snapshot snapshot() { std::lock_guard lock(mutex); return state; }
}
