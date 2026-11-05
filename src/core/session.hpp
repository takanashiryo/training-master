#pragma once
#include "core/chart.hpp"
#include "core/autoplay_mode.hpp"
#include "core/gauge_types.hpp"
#include "core/song_info.hpp"
#include "core/reverse_audio.hpp"
#include "core/training_access.hpp"
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace training {
struct Track {
    std::string chart;
    std::uint32_t music_id{};
    std::uint32_t difficulty{};
    float duration{};
    bool native_quick_finish{};
    std::shared_ptr<const Chart> preview;
    bool autoplay_ready{};
    std::shared_ptr<const SongInfo> info;
    std::shared_ptr<ReverseAsset> reverse_audio;
    bool camera_ready{};
    bool lane_speed_ready{};
    bool gauge_ready{};
    bool song_visuals_ready{};
};
struct State {
    std::uint64_t generation{};
    Track track;
    bool playing{}, enabled{}, tainted{}, paused{}, loop{}, ready{},autoplay{};
    float position{}, in{}, out{}, rate{1.f};
    std::uint64_t loops{}, blocked_results{};
    std::string message{"Waiting for gameplay"};
    bool reversing{};
    bool camera_locked{};
    bool rewinding{}, repeat{};
    bool lane_speed_locked{};
    GaugeConfig gauge;
    bool gauge_returning{};
    float loop_before{1.f},loop_after{1.f};
    bool at_end{};
    bool finish_requested{};
    AutoplayMode autoplay_mode{AutoplayMode::All};
    bool hide_song_visuals{};
    bool scrubbing{};
    TrainingAccess access{TrainingAccess::Allowed};
};
struct LoopRange { float start{},end{}; };
LoopRange loop_range(const State& state);
float loop_gain(const State& state);
enum class Action { Enable, Disable, Pause, Resume, Seek, SetIn, SetOut, SetLoop, SetRate, Restart, Finish, SetRegion, SetAutoplay, Reverse, SetCameraLocked, SetRepeat, SetLaneSpeedLocked, SetGaugeEnabled, SetGaugeType, SetGaugeInitial, CycleLoop, SetAutoplayMode, SetHideSongVisuals, TogglePlayback, ReturnToStart, ScrubBy };
struct Command { std::uint64_t generation; Action action; float value{}, end{}; };
class Backend {
public:
    virtual ~Backend() = default;
    virtual bool pause(bool value) = 0;
    virtual bool seek(float seconds) = 0;
    virtual bool rate(float value) = 0;
    virtual bool finish() = 0;
    virtual bool reverse(bool) { return false; }
    virtual bool sync_reverse_chart(float) { return true; }
    virtual bool mute(bool) { return true; }
    virtual void music_gain(float) {}
    virtual bool lane_speed(bool) { return false; }
    virtual void release_lane_speed() {}
    virtual bool gauge(const GaugeConfig&) { return false; }
    virtual bool reset_gauge() { return true; }
    virtual bool gauge_failed(bool& failed) { failed=false; return true; }
    virtual bool track_complete(bool& complete) { complete=false; return true; }
    virtual void release_gauge() {}
    // Move the visible chart without decoding/seeking song audio each frame.
    virtual bool seek_chart(float seconds) { return seek(seconds); }
};
// Only the game thread calls begin/tick/end; UI threads exchange copies and commands.
class Session {
public:
    void begin(Track track, bool ready, bool previously_tainted=false);
    void end(Backend& backend);
    // The game thread publishes eligibility before binding any track data.
    // Changing to a blocked context releases an existing practice session.
    void set_access(TrainingAccess access,Backend& backend);
    void submit(Command command);
    void tick(float position, bool native_paused, Backend& backend,float elapsed_seconds=0.f);
    State snapshot() const;
    bool navigation_active() const;
    bool suppress_result() const;
    bool native_exit_locked() const;
    void result_blocked();
    void camera_unavailable();
    bool camera_requested() const;
    void lane_speed_unavailable();
    void song_visuals_unavailable();
    void set_loop_buffers(float before,float after);
    // Called at native play-state entry, before it can schedule a failure scene.
    // True holds that state while the ordinary timer hook animates the return.
    bool intercept_gauge_failure(float position,Backend& backend);
    // Keep native player/HUD updates running while holding natural end routing.
    bool intercept_track_end(float position,Backend& backend);
private:
    void end_locked(Backend&);
    bool apply(const Command&, Backend&);
    bool enable(Backend&);
    bool start_rewind(float target, bool loop_return, Backend&,bool reset_native_gauge=false);
    bool release_rewind_mute(Backend&);
    bool pause_at_end(Backend&);
    bool commit_scrub(Backend&);
    bool update_scrub(float elapsed_seconds,Backend&);
    mutable std::mutex mutex_;
    State state_;
    std::deque<Command> commands_;
    float rewind_from_{},rewind_elapsed_{},rewind_duration_{},rewind_target_{};
    bool rewind_muted_{},loop_return_{},rewind_reset_gauge_{};
    float scrub_target_{},scrub_idle_{},scrub_direction_{};
};
}
