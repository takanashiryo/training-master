#pragma once
#include <windows.h>
#include "native/profile_scan.hpp"
#include <string>
namespace training::native {
// Published only after the entire family resolves. Call validate before hooks.
const Addresses& addresses();
const Resolution& resolution();
const WorldLayout& world_layout();
const ResultLayout& result_layout();
// Returns a verified CStageResultGameData object for either native access ABI.
std::uintptr_t stage_result(std::uintptr_t image);
bool validate(HMODULE module, std::string& reason);
}
