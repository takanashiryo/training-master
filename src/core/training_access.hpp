#pragma once
namespace training {
enum class TrainingAccess { Allowed, UnsupportedMode, Demo };
// Native game-mode IDs, independent of start options and course flags.
constexpr bool training_mode_allowed(int mode) { return mode==0 || mode==6; }
}
