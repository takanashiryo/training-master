#pragma once
#include "core/session.hpp"

namespace training {
struct GameInput {
    bool start{},fx_left{},fx_right{};
    std::uint16_t vol_left{};
    bool bt_a{},bt_d{};
};
enum class GameShortcutEvent { None, TogglePlayback, ReturnToStart };

// Sampled on the game thread, independently of overlay visibility and AutoPlay.
class GameShortcuts {
public:
    static constexpr std::uint64_t hold_ms=1000;
    static constexpr float seconds_per_turn=2.f;
    GameShortcutEvent poll(Session& session,const GameInput& input,std::uint64_t now_ms);
    void reset();
private:
    std::uint64_t generation_{},pressed_at_{};
    std::uint16_t last_vol_{};
    bool sampled_{},armed_{},holding_{},long_pressed_{},active_{};
    bool last_bt_a_{},last_bt_d_{},last_loop_chord_{};
};
}
