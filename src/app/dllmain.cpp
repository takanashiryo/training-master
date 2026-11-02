#include "game/game.hpp"
#include "ui/overlay.hpp"
#include "platform/log.hpp"
#include "training_about.hpp"
#include "ui/preferences.hpp"
#include <MinHook.h>
namespace {
DWORD WINAPI start(void* module) {
    training::open_log(static_cast<HMODULE>(module));
    std::string config_error;
    if(!training::initialize_preferences(static_cast<HMODULE>(module),config_error)) training::log(config_error);
    training::log(std::string("Training Master ")+training::about::build_date+" starting (disabled by default)");
    const auto mh=MH_Initialize();
    if (mh!=MH_OK) { training::log("MinHook initialization failed"); return 1; }
    HMODULE game{};
    for (int i=0;i<1200 && !game;++i) { game=GetModuleHandleW(L"soundvoltex.dll"); if (!game) Sleep(100); }
    if (!game) { training::log("soundvoltex.dll was not loaded within 120 seconds"); return 2; }
    std::string reason;
    if (!training::install_overlay(training::game_session(),reason)) { training::log(reason); return 3; }
    training::log(reason);
    const bool ok=training::install_game_hooks(game,reason);
    training::set_overlay_status(reason,!ok);
    training::log(reason);
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
