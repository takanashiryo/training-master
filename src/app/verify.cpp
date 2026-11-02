#include "native/profile.hpp"
#include "game/autoplay_resolver.hpp"
#include <MinHook.h>
#include <iostream>
#include <cstring>
#include <vector>
#include <string_view>
// Inspect a mapped image without resolving imports, executing game code or DllMain.
void dummy() {}
int wmain(int argc,wchar_t** argv) {
    const bool require_autoplay=argc==3 && std::wstring_view(argv[2])==L"--require-autoplay";
    if(argc!=2 && !require_autoplay) { std::wcerr<<L"Usage: training_verify.exe <soundvoltex.dll> [--require-autoplay]\n"; return 1; }
    auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!module) { std::cerr<<"Map failed: "<<GetLastError()<<'\n'; return 2; }
    std::string reason;
    if(!training::native::validate(module,reason)) { std::cerr<<reason<<'\n'; FreeLibrary(module); return 3; }
    std::cout<<reason<<'\n';
    std::cout<<"RTTI interface methods verified: "<<training::native::resolution().interface_methods<<'\n';
    for (const auto& detail:training::native::resolution().details) std::cout<<detail<<'\n';
    if(MH_Initialize()!=MH_OK) { FreeLibrary(module); return 4; }
    using namespace training::native;
    const auto& a=addresses();
    const auto& w=world_layout();
    std::cout<<"World layout: music="<<w.music<<", renderer="<<w.renderer
        <<", forced_end="<<w.forced_end<<", gauge_types="<<w.gauge_types
        <<", no_fail="<<w.no_fail<<", battle="<<w.battle<<'\n';
    const auto& r=result_layout();
    std::cout<<"Result layout: stride="<<r.stride<<", excluded="<<r.excluded
        <<", tail_shift="<<r.tail_shift<<", fixed_timed_limit="<<r.fixed_timed_limit
        <<", chart_gauge_gain="<<resolution().chart_gauge_gain<<'\n';
    std::vector<std::uintptr_t> targets{a.world_update,a.world_destroy,a.collect_result,a.timer_update,a.audio_feed,a.request_next,a.serialize_result,a.can_continue,a.flow_decision,a.camera_update,a.lane_timing_constructor,a.hisp_update,a.hisp_event,a.gauge_hud_event,a.play_state,a.posteffect_event,a.posteffect_shake};
    const auto automatic=training::autoplay_discovery::resolve(module);
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    if (automatic && automatic.layout.update==base+a.world_update && automatic.layout.world_vtable==base+a.world_vtable &&
        automatic.layout.timer_vtable==base+a.game_timer_vtable) {
        targets.push_back(automatic.layout.judge-base);
        if(automatic.layout.judgment_event && automatic.layout.judgment_gauge==base+a.gauge_update) {
            const auto code=reinterpret_cast<const unsigned char*>(automatic.layout.judgment_event);
            std::uintptr_t secondary{};
            for(const unsigned offset:{539u,569u}) if(code[offset-3]==0x4c && code[offset-2]==0x8d && code[offset-1]==0x05) {
                std::int32_t displacement{}; std::memcpy(&displacement,code+offset,4);
                secondary=automatic.layout.judgment_event+offset+4+displacement;
                break;
            }
            if(!secondary || *reinterpret_cast<const std::uintptr_t*>(secondary+136)!=base+a.gauge_failed) {
                std::cerr<<"Judgment callback secondary gauge interface mismatch\n";
                MH_Uninitialize(); FreeLibrary(module); return 8;
            }
            targets.push_back(automatic.layout.judgment_event-base);
            std::cout<<"Judgment callback RVA=0x"<<std::hex<<automatic.layout.judgment_event-base<<std::dec<<'\n';
        } else {
            std::cout<<"Judgment callback unavailable or disagrees with gauge updater\n";
            if(require_autoplay) { MH_Uninitialize(); FreeLibrary(module); return 7; }
        }
        if(automatic.layout.judgment_color_option)
            std::cout<<"EARLY/LATE color option RVA=0x"<<std::hex<<automatic.layout.judgment_color_option-base<<std::dec<<'\n';
        else if(require_autoplay) { std::cerr<<"EARLY/LATE color option unavailable\n"; MH_Uninitialize(); FreeLibrary(module); return 9; }
        if(automatic.layout.s_critical_flag && automatic.layout.s_critical_flag+4==training::native::world_layout().gauge_types)
            std::cout<<"S-CRITICAL flag offset=0x"<<std::hex<<automatic.layout.s_critical_flag<<std::dec<<'\n';
        else if(require_autoplay) { std::cerr<<"S-CRITICAL flag unavailable or disagrees with world layout\n"; MH_Uninitialize(); FreeLibrary(module); return 10; }
        std::cout<<"Native AutoPlay discovered; AutoJudge RVA=0x"<<std::hex<<automatic.layout.judge-base<<std::dec<<'\n';
    } else {
        std::cout<<"Native AutoPlay unavailable: "<<(automatic?"training layout mismatch":automatic.error)<<'\n';
        if (require_autoplay) { MH_Uninitialize(); FreeLibrary(module); return 6; }
    }
    bool ok=true;
    for(auto rva:targets) {
        auto* address=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(module)+rva);
        void* original{};
        const auto status=MH_CreateHook(address,reinterpret_cast<void*>(dummy),&original);
        std::cout<<"RVA 0x"<<std::hex<<rva<<": "<<MH_StatusToString(status)<<'\n';
        if(status!=MH_OK) ok=false; else MH_RemoveHook(address);
    }
    MH_Uninitialize(); FreeLibrary(module);
    std::cout<<"No hooks enabled; no game code executed.\n";
    return ok?0:5;
}
