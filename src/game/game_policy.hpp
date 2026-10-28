#pragma once
#include <cstdint>
namespace training {
bool supports_training_world(std::uintptr_t image,std::uintptr_t world);
bool prepare_training_finish(std::uintptr_t image,std::uintptr_t world);
bool suppress_native_finish(std::uintptr_t image,std::uintptr_t world,bool exit_locked);
bool native_track_complete(std::uintptr_t image,std::uintptr_t world,bool& complete);
}
