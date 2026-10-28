#include "game/autoplay.hpp"
#include <atomic>

namespace training::autoplay {
namespace {
autoplay_discovery::Layout layout;
std::atomic<std::uint64_t> updates{},overrides{},faults{},last_tick{};
std::array<std::atomic<std::uint64_t>,11> judgments{};
std::atomic<float> last_error{};
std::atomic<bool> native{},effective{},faulted{};
unsigned char* flags(std::uintptr_t world) {
    __try {
        if (*reinterpret_cast<std::uintptr_t*>(world)!=layout.world_vtable) { ++faults; faulted=true; return nullptr; }
        const auto context=*reinterpret_cast<std::uintptr_t*>(world+layout.context);
        if (!context) return nullptr;
        if (*reinterpret_cast<std::uintptr_t*>(context)!=layout.context_vtable) {
            ++faults; faulted=true; return nullptr;
        }
        const auto value=*reinterpret_cast<unsigned char**>(context+layout.flags);
        if (!value) return nullptr;
        if (*value>1) { ++faults; faulted=true; return nullptr; }
        return value;
    } __except(EXCEPTION_EXECUTE_HANDLER) { ++faults; faulted=true; return nullptr; }
}
}
void configure(const autoplay_discovery::Layout& value) {
    layout=value; begin_track(); clear_counters();
}
void begin_track() { faulted=false; native=false; effective=false; last_tick=0; }
void update(std::uintptr_t world,bool enabled,Update original) {
    ++updates; last_tick=GetTickCount64();
    auto flag=layout.world_vtable && !faulted.load()?flags(world):nullptr;
    const unsigned char before=flag?*flag:0;
    const bool force=flag && enabled;
    native=before!=0; effective=before!=0 || force;
    if (force) { *flag=1; ++overrides; }
    __try { original(world); }
    __finally { if (force) *flag=before; }
}
std::uintptr_t judge(Judge original,std::uintptr_t referee,std::uintptr_t output,std::uintptr_t note,float time) {
    const auto result=original(referee,output,note,time);
    __try {
        const auto kind=*reinterpret_cast<unsigned*>(result);
        if (kind>0 && kind<judgments.size()) {
            ++judgments[kind]; last_error=*reinterpret_cast<float*>(result+8);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ++faults; }
    return result;
}
Snapshot snapshot() {
    Snapshot s; s.updates=updates; s.overrides=overrides; s.faults=faults; s.last_tick=last_tick;
    s.last_error=last_error; s.native=native; s.effective=effective; s.faulted=faulted;
    for (std::size_t i=0;i<s.judgments.size();++i) s.judgments[i]=judgments[i];
    return s;
}
void clear_counters() {
    updates=0; overrides=0; faults=0; last_error=0;
    for (auto& count:judgments) count=0;
}
}
