#include "core/session.hpp"
#include "core/playback_rate.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace training {
LoopRange loop_range(const State& s) {
    const double duration=std::max(0.,static_cast<double>(s.track.duration));
    return {static_cast<float>(std::clamp(static_cast<double>(s.in)-s.loop_before,0.,duration)),
            static_cast<float>(std::clamp(static_cast<double>(s.out)+s.loop_after,0.,duration))};
}
float loop_gain(const State& s) {
    if(!s.enabled || !s.loop) return 1.f;
    const auto range=loop_range(s);
    if(s.position<s.in && s.in>range.start)
        return std::clamp((s.position-range.start)/(s.in-range.start),0.f,1.f);
    if(s.position>s.out && range.end>s.out)
        return std::clamp((range.end-s.position)/(range.end-s.out),0.f,1.f);
    return 1.f;
}
void Session::set_loop_buffers(float before,float after) {
    if(!std::isfinite(before) || before<0 || !std::isfinite(after) || after<0) return;
    std::lock_guard lock(mutex_); state_.loop_before=before; state_.loop_after=after;
}
void Session::begin(Track track, bool ready, bool previously_tainted) {
    std::lock_guard lock(mutex_);
    const auto generation = state_.generation + 1;
    const auto blocked = state_.blocked_results;
    const auto before=state_.loop_before,after=state_.loop_after;
    state_ = {};
    state_.loop_before=before; state_.loop_after=after;
    state_.generation = generation;
    state_.blocked_results = blocked;
    state_.track = std::move(track);
    state_.playing = true;
    state_.tainted = previously_tainted;
    state_.ready = ready && std::isfinite(state_.track.duration) && state_.track.duration > .05f;
    state_.out = state_.track.duration;
    state_.message = state_.ready ? "Training is off" : "Track is not supported";
    commands_.clear();
    rewind_from_=rewind_elapsed_=rewind_duration_=rewind_target_=0.f; rewind_muted_=loop_return_=false;
}
void Session::end(Backend& backend) {
    std::lock_guard lock(mutex_);
    backend.music_gain(1.f);
    backend.release_gauge(); state_.gauge.enabled=false;
    backend.release_lane_speed(); state_.lane_speed_locked=false;
    if (state_.enabled) {
        if (state_.reversing) backend.reverse(false);
        backend.rate(1.f); if (state_.paused || state_.reversing) backend.pause(false);
    }
    if (rewind_muted_) backend.mute(false);
    rewind_muted_=false; state_.rewinding=state_.gauge_returning=state_.at_end=false;
    state_.playing = state_.enabled = state_.paused = state_.autoplay = false;
    state_.hide_song_visuals=false;
    state_.rate = 1.f;
    state_.reversing = state_.camera_locked = false;
    // Keep taint until the next begin: result collection may follow world teardown.
    commands_.clear();
    state_.message = "Waiting for gameplay";
}
void Session::submit(Command command) {
    std::lock_guard lock(mutex_);
    // A slow game frame may receive many slider samples. Only the latest
    // consecutive target matters; preserve ordering around all other actions.
    if(command.action==Action::Seek && !commands_.empty() &&
       commands_.back().action==Action::Seek && commands_.back().generation==command.generation) {
        commands_.back()=command; return;
    }
    if (commands_.size() < 128) commands_.push_back(command);
}
State Session::snapshot() const { std::lock_guard lock(mutex_); return state_; }
bool Session::navigation_active() const {
    std::lock_guard lock(mutex_);
    return state_.rewinding || (state_.reversing && !state_.paused) ||
        std::any_of(commands_.begin(),commands_.end(),[&](const auto& c) {
            return c.generation==state_.generation && c.action==Action::Seek;
        });
}
bool Session::suppress_result() const { std::lock_guard lock(mutex_); return state_.tainted; }
bool Session::native_exit_locked() const { std::lock_guard lock(mutex_); return state_.tainted && !state_.finish_requested; }
void Session::result_blocked() { std::lock_guard lock(mutex_); ++state_.blocked_results; }
void Session::camera_unavailable() {
    std::lock_guard lock(mutex_);
    state_.camera_locked=state_.track.camera_ready=false;
    state_.message="Camera control is unavailable for this track";
}
bool Session::camera_requested() const {
    std::lock_guard lock(mutex_);
    return state_.playing && state_.enabled && state_.camera_locked;
}
void Session::lane_speed_unavailable() {
    std::lock_guard lock(mutex_);
    state_.track.lane_speed_ready=state_.lane_speed_locked=false;
    state_.ready=false; state_.paused=true; state_.rewinding=state_.reversing=false;
    state_.autoplay=state_.camera_locked=false;
    state_.message="Lane Speed control failed; paused. Results remain blocked.";
}
void Session::song_visuals_unavailable() {
    std::lock_guard lock(mutex_);
    state_.hide_song_visuals=state_.track.song_visuals_ready=false;
    state_.message="Song visual control is unavailable for this track";
}
bool Session::enable(Backend& b) {
    auto& s=state_;
    if (s.enabled) return true;
    // Latch protection before muting, pausing or changing the native clock.
    s.tainted=s.enabled=true;
    return start_rewind(0.f,false,b);
}
bool Session::intercept_gauge_failure(float position,Backend& b) {
    std::lock_guard lock(mutex_);
    auto& s=state_;
    if(!s.playing || !s.tainted || s.finish_requested || !s.track.gauge_ready) return false;
    if(s.gauge_returning) return true;
    if(!s.ready) return false;
    bool failed{};
    const bool valid=b.gauge_failed(failed) && std::isfinite(position);
    if(valid && !failed) return false;
    s.gauge_returning=true; // Hold native failure/finish routing before transport.
    if(valid) {
        s.position=std::clamp(position,0.f,s.track.duration);
        if(s.rewinding || start_rewind(s.loop?loop_range(s).start:0.f,s.loop,b)) return true;
    }
    b.pause(true); s.paused=true; s.ready=false; s.rewinding=false;
    s.autoplay=s.camera_locked=false;
    s.message="Gauge return failed; paused. Results remain blocked.";
    return true;
}
bool Session::start_rewind(float target,bool loop_return,Backend& b) {
    auto& s=state_;
    if (!b.mute(true)) return false;
    rewind_muted_=true;
    if (s.reversing && !b.reverse(false)) return false;
    if (!b.pause(true)) return false;
    s.paused=true; s.rewinding=true; s.at_end=false;
    rewind_from_=std::clamp(s.position,0.f,s.track.duration); rewind_elapsed_=0.f;
    rewind_target_=target; loop_return_=loop_return;
    rewind_duration_=std::clamp(std::fabs(rewind_from_-target)/120.f,.35f,1.25f);
    return true;
}
bool Session::release_rewind_mute(Backend& b) {
    if (rewind_muted_ && !b.mute(false)) return false;
    rewind_muted_=false; return true;
}
bool Session::pause_at_end(Backend& b) {
    // Never seek here: seeking rebuilds native note/judgment history and could
    // suppress a just-earned UC/PUC/S-PUC announcement in the player updater.
    state_.at_end=true; state_.position=state_.track.duration; state_.paused=true;
    return b.pause(true);
}
bool Session::intercept_track_end(float position,Backend& b) {
    std::lock_guard lock(mutex_);
    auto& s=state_;
    if(!s.playing || !s.tainted || s.finish_requested) return false;
    if(s.at_end) return true;
    bool complete{};
    if(!std::isfinite(position) || !b.track_complete(complete)) {
        pause_at_end(b); s.ready=false; s.autoplay=s.camera_locked=false;
        s.message="Track end control failed; paused. Use Finish Immediately.";
        return true;
    }
    if(!complete && position<s.track.duration) return false;
    // Loop/reverse/rewind own transport; hold the native exit only, allowing
    // their existing return/continuation logic and the HUD animation to run.
    if(s.loop || s.reversing || s.rewinding) return true;
    if(!pause_at_end(b)) {
        s.ready=false; s.autoplay=s.camera_locked=false;
        s.message="Track end pause failed. Use Finish Immediately.";
    }
    return true;
}
bool Session::apply(const Command& c, Backend& b) {
    auto& s = state_;
    if (c.generation != s.generation || !s.playing || !std::isfinite(c.value)) return true;
    if(s.finish_requested) return true;
    if (c.action==Action::SetGaugeEnabled || c.action==Action::SetGaugeType || c.action==Action::SetGaugeInitial) {
        if (c.action==Action::SetGaugeEnabled && !c.value) {
            auto config=s.gauge; config.enabled=false;
            if (s.gauge.enabled && !b.gauge(config)) return false;
            s.gauge=config; return true;
        }
        if (!s.ready || !s.track.gauge_ready) return true;
        auto config=s.gauge;
        if (c.action==Action::SetGaugeEnabled) config.enabled=true;
        else if (!config.enabled) return true;
        else if (c.action==Action::SetGaugeType) {
            if (c.value<0 || c.value>=static_cast<float>(GaugeType::Count) || std::floor(c.value)!=c.value) return true;
            config.type=static_cast<GaugeType>(static_cast<int>(c.value));
        } else {
            if (c.value<0 || c.value>100) return true;
            config.initial=c.value;
        }
        if (!enable(b)) return false;
        s.gauge.enabled=true; // Retain cleanup and result protection on failure.
        if (!b.gauge(config)) return false;
        s.gauge=config; return true;
    }
    if (c.action==Action::SetLaneSpeedLocked) {
        if (!c.value) {
            if (s.lane_speed_locked && !b.lane_speed(false)) return false;
            s.lane_speed_locked=false; return true;
        }
        if (!s.ready || !s.track.lane_speed_ready) { s.message="Lane Speed control is unavailable for this track"; return true; }
        if (!enable(b)) return false;
        if (!s.lane_speed_locked) {
            // Preserve the cleanup obligation even if native rebuilding fails.
            s.lane_speed_locked=true;
            if (!b.lane_speed(true)) return false;
        }
        s.message="Practice track: results and upload disabled";
        return true;
    }
    if (c.action==Action::SetCameraLocked) {
        if (!c.value) { s.camera_locked=false; return true; }
        if (!s.ready || !s.track.camera_ready) { s.message="Camera control is unavailable for this track"; return true; }
        if (!enable(b)) return false;
        s.camera_locked=true;
        s.message="Practice track: results and upload disabled";
        return true;
    }
    if(c.action==Action::SetHideSongVisuals) {
        s.hide_song_visuals=c.value!=0 && s.enabled && s.ready && s.track.song_visuals_ready;
        return true;
    }
    if (c.action==Action::SetAutoplayMode) {
        if(std::isfinite(c.value) && c.value>=0 && c.value<=2 && std::floor(c.value)==c.value)
            s.autoplay_mode=static_cast<AutoplayMode>(static_cast<int>(c.value));
        return true;
    }
    if (c.action==Action::SetAutoplay) {
        if (!c.value) { s.autoplay=false; return true; }
        if (!s.ready || !s.track.autoplay_ready) { s.message="Native AutoPlay is unavailable for this track"; return true; }
        // Latch the same no-result guard BEFORE any native automatic judgment.
        if (!enable(b)) return false;
        s.autoplay=true;
        s.message="AutoPlay enabled; results and upload disabled";
        return true;
    }
    if (c.action == Action::Enable) {
        if (!s.ready) { s.message = "Required game hooks are unavailable"; return true; }
        // Irreversible for this track, before any transport operation.
        if (!enable(b)) return false;
        s.message = "Practice track: results and upload disabled";
        return true;
    }
    if (!s.enabled && !(c.action==Action::Finish && s.tainted)) return true;
    switch (c.action) {
    case Action::Disable:
        if (s.gauge.enabled) { auto config=s.gauge; config.enabled=false; if(!b.gauge(config)) return false; s.gauge.enabled=false; }
        if (s.lane_speed_locked && !b.lane_speed(false)) return false;
        s.lane_speed_locked=false;
        if ((s.rewinding && !b.seek(s.position)) || (s.reversing && !b.reverse(false)) ||
            !b.rate(1.f) || ((s.paused || s.reversing) && !b.pause(false)) || !release_rewind_mute(b)) return false;
        s.enabled = s.paused = s.loop = s.repeat = s.autoplay = s.hide_song_visuals = false; s.rate = 1.f;
        s.reversing=s.camera_locked=s.rewinding=s.gauge_returning=s.at_end=false;
        s.message = "Training off; this track still cannot save results";
        break;
    case Action::Pause:
        if ((s.rewinding && !b.seek(s.position)) || !b.pause(true)) return false;
        s.rewinding=s.gauge_returning=false; s.paused=true; break;
    case Action::Resume:
        if(s.reversing && !b.reverse(false)) return false;
        if(s.at_end || s.position>=s.track.duration) {
            if(!b.seek(0.f) || ((s.gauge.enabled || s.track.gauge_ready) && !b.reset_gauge())) return false;
            s.position=0.f;
        } else if(s.rewinding && !b.seek(s.position)) return false;
        if(!b.pause(false) || !release_rewind_mute(b)) return false;
        s.paused = s.reversing = s.rewinding = s.gauge_returning = s.at_end = false; break;
    case Action::Reverse:
        if (!s.track.reverse_audio || !s.track.reverse_audio->clip.load()) {
            s.message="Reverse audio is loading or unavailable"; break;
        }
        if (!b.reverse(true) || !release_rewind_mute(b)) return false;
        s.rewinding=s.gauge_returning=s.at_end=false;
        s.reversing=true; s.paused=false;
        break;
    case Action::Seek:
        s.position = std::clamp(c.value, 0.f, s.track.duration);
        if(s.loop && !s.paused) {
            const auto range=loop_range(s);
            s.position=s.reversing?std::min(s.position,range.end):std::max(s.position,range.start);
        }
        if (!b.seek(s.position)) return false;
        if ((s.gauge.enabled || s.gauge_returning) && (s.position<=.001f || std::fabs(s.position-s.in)<=.001f ||
            (s.loop && std::fabs(s.position-loop_range(s).start)<=.001f)) && !b.reset_gauge()) return false;
        s.rewinding=s.gauge_returning=s.at_end=false;
        break;
    case Action::SetIn:
        if (c.value < 0.f || c.value > s.out - .05f) { s.message = "IN must be at least 50 ms before OUT"; break; }
        s.in = c.value; break;
    case Action::SetOut:
        if (c.value < s.in + .05f || c.value > s.track.duration) { s.message = "OUT must follow IN and stay inside the track"; break; }
        s.out = c.value; break;
    case Action::SetRegion:
        if (!std::isfinite(c.end) || c.value<0.f || c.end<c.value+.05f || c.end>s.track.duration) {
            s.message = "Region must stay inside the track and be at least 50 ms long"; break;
        }
        // Moving a selection must not validate one endpoint against the old
        // opposite endpoint. Publish the pair together on the game thread.
        s.in=c.value; s.out=c.end; break;
    case Action::SetLoop: s.loop = c.value != 0.f; break;
    case Action::SetRepeat: s.repeat = s.loop && c.value != 0.f; break;
    case Action::CycleLoop:
        // Resolve each click on the game thread so rapid queued clicks cannot
        // repeat a transition based on the same stale overlay snapshot.
        if(!s.loop) s.loop=s.repeat=true;
        else if(s.repeat) s.repeat=false;
        else s.loop=s.repeat=false;
        break;
    case Action::SetRate:
        if (c.value < min_playback_rate || c.value > max_playback_rate) { s.message = "Rate must be between 0.25 and 2.00"; break; }
        if (!b.rate(c.value)) return false;
        s.rate = c.value; break;
    case Action::Restart:
        s.position=s.loop?loop_range(s).start:s.in;
        if (!b.seek(s.position) || ((s.gauge.enabled || s.gauge_returning) && !b.reset_gauge())) return false;
        s.rewinding=s.gauge_returning=s.at_end=false; break;
    case Action::Finish:
        b.release_gauge(); s.gauge.enabled=false;
        b.release_lane_speed(); s.lane_speed_locked=false;
        if ((s.reversing && !b.reverse(false)) || !b.rate(1.f) || !b.pause(false) || !b.finish() || !release_rewind_mute(b)) return false;
        s.finish_requested=true;
        s.enabled = s.paused = s.loop = s.repeat = s.autoplay = s.hide_song_visuals = false; s.rate = 1.f;
        s.reversing=s.camera_locked=s.rewinding=s.gauge_returning=s.at_end=false;
        s.message = "Practice finished; result collection remains blocked";
        break;
    default: break;
    }
    return true;
}
void Session::tick(float position, bool native_paused, Backend& b,float elapsed_seconds) {
    std::lock_guard lock(mutex_);
    if (!state_.playing) return;
    if (!std::isfinite(position)) {
        if (state_.reversing) { b.reverse(false); b.pause(true); state_.reversing=false; state_.paused=true; }
        state_.rewinding=false;
        state_.ready = false; state_.autoplay=state_.camera_locked=false; state_.message = "Invalid game clock"; return;
    }
    if (!state_.rewinding) state_.position = state_.at_end?state_.track.duration:position;
    // During reverse playback the native timer/music voices are deliberately paused.
    // Logical pause is controlled independently while decoded PCM drives chart time.
    if (state_.rewinding || !state_.reversing) state_.paused = state_.rewinding || state_.at_end || native_paused;
    bool ok = true;
    while (!commands_.empty()) {
        const auto c = commands_.front(); commands_.pop_front();
        if (!apply(c,b)) { ok = false; break; }
    }
    if (ok && !state_.rewinding && state_.enabled && !state_.paused && state_.loop) {
        const auto range=loop_range(state_);
        // Constrain entry as well as exit: starting/seeking before the lead-in
        // must not play unrelated music from the song's beginning.
        const bool outside_entry=state_.reversing?state_.position>range.end:state_.position<range.start;
        if(outside_entry) {
            const float target=state_.reversing?range.end:range.start;
            ok=b.seek(target);
            if(ok && (state_.gauge.enabled || state_.gauge_returning)) ok=b.reset_gauge();
            if(ok) state_.position=target;
        }
        const bool boundary=state_.reversing?state_.position<=range.start:state_.position>=range.end;
        if (ok && boundary) ok=start_rewind(state_.reversing?range.end:range.start,true,b);
    }
    if (ok && state_.rewinding) {
        if (std::isfinite(elapsed_seconds)) rewind_elapsed_+=std::clamp(elapsed_seconds,0.f,.05f);
        const float remaining=std::max(0.f,1.f-rewind_elapsed_/rewind_duration_);
        // Entry and loop share the same cubic ease-out, ending at the exact target.
        const float target=rewind_target_+(rewind_from_-rewind_target_)*remaining*remaining*remaining;
        if (std::fabs(rewind_from_-rewind_target_)<=.001f || remaining==0.f) {
            ok=b.seek(rewind_target_) && b.pause(true);
            if (ok && (state_.gauge.enabled || state_.gauge_returning)) ok=b.reset_gauge();
            const bool resume=loop_return_ && state_.loop && state_.repeat;
            if (ok && resume) {
                if (state_.reversing) ok=b.reverse(true);
                if (ok) ok=b.pause(false) && release_rewind_mute(b);
            }
            if (ok) {
                state_.position=rewind_target_; state_.paused=!resume; state_.rewinding=state_.gauge_returning=false;
                if (loop_return_) ++state_.loops;
            }
            // A non-repeating return remains muted until explicit Play/Reverse or exit.
        } else {
            ok=b.seek_chart(target);
            if (ok) state_.position=target;
        }
    } else if (ok && state_.enabled && !state_.paused) {
        if (state_.reversing && state_.position<=0.f) {
            ok=b.pause(true);
            if (ok && state_.gauge.enabled) ok=b.reset_gauge();
            if (ok) { state_.position=0; state_.paused=true; }
        }
    }
    if(ok && state_.tainted && !state_.finish_requested && !state_.loop && !state_.reversing && !state_.rewinding &&
       !state_.at_end && state_.position>=state_.track.duration) ok=pause_at_end(b);
    b.music_gain(loop_gain(state_));
    if (ok && state_.enabled && state_.reversing && !state_.rewinding) ok=b.sync_reverse_chart(state_.position);
    if (!ok) {
        if (state_.reversing) b.reverse(false);
        b.pause(true); b.rate(1.f);
        state_.reversing=false;
        state_.rewinding=false;
        state_.ready = false; state_.paused = true;
        state_.autoplay=state_.camera_locked=false;
        state_.message = "Transport failed; paused. Results remain blocked. Use Finish Immediately.";
        commands_.clear();
    }
}
}
