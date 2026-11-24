#include "game/game.hpp"
#include "ui/overlay.hpp"
#include "platform/log.hpp"
#include "training_about.hpp"
#include "ui/preferences.hpp"
#include <MinHook.h>
namespace {
DWORD WINAPI start(void* module) {
    using training::LogLevel;
    training::open_log(static_cast<HMODULE>(module));
    training::log(LogLevel::Info,std::string("Training Master ")+training::about::build_date+" starting");
    training::log(LogLevel::Debug,"Debug logging enabled");
    std::string config_error;
    if (!training::initialize_preferences(static_cast<HMODULE>(module),config_error)) training::log(LogLevel::Warn,config_error);
    else training::log(LogLevel::Debug,"Preferences initialized from trainingmaster.ini or defaults");
    const auto mh=MH_Initialize();
    training::debug_log("MinHook initialization: %s",MH_StatusToString(mh));
    if (mh!=MH_OK) { training::log(LogLevel::Error,"MinHook initialization failed"); return 1; }
    const auto wait_start=GetTickCount64();
    HMODULE game{};
    for (int i=0;i<1200 && !game;++i) { game=GetModuleHandleW(L"soundvoltex.dll"); if (!game) Sleep(100); }
    training::debug_log("Waited %llu ms for soundvoltex.dll; module=%p",GetTickCount64()-wait_start,static_cast<void*>(game));
    if (!game) { training::log(LogLevel::Error,"soundvoltex.dll was not loaded within 120 seconds"); return 2; }
    std::string reason;
    if (!training::install_overlay(training::game_session(),reason)) { training::log(LogLevel::Error,reason); return 3; }
    training::log(LogLevel::Debug,reason);
    const bool ok=training::install_game_hooks(game,reason);
    training::set_overlay_failed(!ok);
    if (ok) training::log(LogLevel::Info,reason);
    else {
        training::log(LogLevel::Error,"Game hooks failed to initialize. Launch with --debug for details.");
        training::log(LogLevel::Debug,reason);
    }
    return ok?0:4;
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // Keep code resident until process exit; runtime FreeLibrary is not supported.
        HMODULE pin{}; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&DllMain),&pin);
        if (const auto thread=CreateThread(nullptr,0,start,module,0,nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
