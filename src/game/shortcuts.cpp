#include "game/shortcuts.hpp"

namespace training {
void GameShortcuts::reset() { *this={}; }
GameShortcutEvent GameShortcuts::poll(Session& session,const GameInput& input,std::uint64_t now_ms) {
    const auto state=session.snapshot();
    const bool active=state.playing && state.enabled && state.ready && !state.finish_requested && !state.rewinding;
    if(!sampled_ || state.generation!=generation_ || active!=active_) {
        reset(); sampled_=true; generation_=state.generation; last_vol_=input.vol_left; active_=active;
    }
    int delta=static_cast<int>(input.vol_left)-last_vol_;
    if(delta>32767) delta-=65536;
    else if(delta<-32768) delta+=65536;
    last_vol_=input.vol_left;
    if(!active) return GameShortcutEvent::None;
    auto event=GameShortcutEvent::None;

    const bool chord=input.start && input.fx_left && input.fx_right;
    const bool released=!input.start && !input.fx_left && !input.fx_right;
    if(released && !holding_) armed_=true;
    if(chord && armed_ && !holding_) {
        holding_=true; armed_=false; long_pressed_=false; pressed_at_=now_ms;
        // Pause at the press position, before waiting for a possible hold.
        // Release only rearms the gesture; it must not toggle a second time.
        session.submit({state.generation,Action::TogglePlayback});
        event=GameShortcutEvent::TogglePlayback;
    }
    if(holding_) {
        // Only a currently held chord proves a long press. A delayed up sample
        // must not count the paused/unsampled interval as time spent holding.
        if(chord && !long_pressed_ && now_ms>=pressed_at_ && now_ms-pressed_at_>=hold_ms) {
            session.submit({state.generation,Action::ReturnToStart}); long_pressed_=true;
            event=GameShortcutEvent::ReturnToStart;
        }
        if(!chord) {
            holding_=false; armed_=released;
        }
        return event;
    }
    if(state.paused && !chord && delta)
        session.submit({state.generation,Action::ScrubBy,delta*(seconds_per_turn/65536.f)});
    return GameShortcutEvent::None;
}
}
