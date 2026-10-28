#pragma once
#include "core/session.hpp"
#include <windows.h>
#include <string>
namespace training {
Session& game_session();
bool install_game_hooks(HMODULE module, std::string& reason);
}
