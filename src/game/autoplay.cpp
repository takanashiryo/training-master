#include "game/autoplay.hpp"
#include <atomic>

namespace training::autoplay {
namespace {
autoplay_discovery::Layout layout;
std::atomic<std::uint64_t> updates{},overrides{},faults{},last_tick{};
std::array<std::atomic<std::uint64_t>,11> judgments{};
std::atomic<float> last_error{};
std::atomic<bool> native{},effective{},faulted{};
thread_local unsigned char* selective_flag{};
thread_local AutoplayMode selective_mode{AutoplayMode::All};
thread_local unsigned char selective_native{};
unsigned note_family(std::uintptr_t note) {
    __try {
        if(!note) return 0;
        const auto object=*reinterpret_cast<const std::uintptr_t*>(note);
        if(!object) return 0;
        const auto table=*reinterpret_cast<const std::uintptr_t*>(object+8);
        const auto points=reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t)>(
            *reinterpret_cast<const std::uintptr_t*>(table+16))(object+8);
        if(!points) return 0;
        const auto begin=*reinterpret_cast<const std::uintptr_t*>(points);
        const auto end=*reinterpret_cast<const std::uintptr_t*>(points+8);
        if(!begin || end<=begin || (end-begin)%48) return 0;
        return *reinterpret_cast<const unsigned char*>(begin+36);
    } __except(EXCEPTION_EXECUTE_HANDLER) { ++faults; return 0; }
}
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
void update(std::uintptr_t world,bool enabled,Update original,AutoplayMode mode) {
    ++updates; last_tick=GetTickCount64();
    auto flag=layout.world_vtable && !faulted.load()?flags(world):nullptr;
    const unsigned char before=flag?*flag:0;
    const bool force=flag && enabled && valid_autoplay_mode(static_cast<int>(mode)) &&
        (mode==AutoplayMode::All || layout.note_gate);
    const auto previous_flag=selective_flag;
    const auto previous_mode=selective_mode;
    const auto previous_native=selective_native;
    selective_flag=force && mode!=AutoplayMode::All?flag:nullptr;
    selective_mode=mode; selective_native=before;
    native=before!=0; effective=before!=0 || force;
    // Partial modes must sample real input before entering the note loop.
    if (force) { *flag=mode==AutoplayMode::All?1:before; ++overrides; }
    __try { original(world); }
    __finally {
        if (force) *flag=before;
        selective_flag=previous_flag; selective_mode=previous_mode; selective_native=previous_native;
    }
}
bool note_gate(NoteGate original,std::uintptr_t referee,std::uintptr_t note) {
    if(selective_flag) {
        const auto family=note_family(note);
        const bool selected=(selective_mode==AutoplayMode::Buttons && family==1) ||
            (selective_mode==AutoplayMode::Knobs && family==2);
        *selective_flag=selected?1:selective_native;
    }
    return original(referee,note);
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
