#pragma once
namespace training {
enum class AutoplayMode { All, Buttons, Knobs };
constexpr bool valid_autoplay_mode(int value) { return value>=0 && value<=2; }
}
