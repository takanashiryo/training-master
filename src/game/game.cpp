#include "game/game.hpp"
#include "core/audio_envelope.hpp"
#include "native/profile.hpp"
#include "game/scene_transition.hpp"
#include "game/practice_stage.hpp"
#include "game/game_policy.hpp"
#include "platform/log.hpp"
#include "game/autoplay.hpp"
#include "game/camera.hpp"
#include "game/post_effect.hpp"
#include "game/lane_speed.hpp"
#include "game/gauge.hpp"
#include <MinHook.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace training {
namespace {
using Ptr=std::uintptr_t;
template<class T> T& field(Ptr p, std::size_t offset) { return *reinterpret_cast<T*>(p+offset); }
template<class T> T fn(Ptr rva);
Ptr image{}, current_world{};
SongCatalog song_catalog;
std::filesystem::path contents_root;
ReversePlayer reverse_player;
std::shared_ptr<ReverseAsset> reverse_asset;
bool reverse_reported{};
camera::Controller camera_control;
lane_speed::Controller lane_control;
gauge::Controller gauge_control;
bool lane_fault{},lane_reported{};
lane_speed::Construct original_timing_constructor{};
using LaneUpdate=Ptr(__fastcall*)(Ptr);
using LaneEvent=Ptr(__fastcall*)(Ptr,Ptr);
LaneUpdate original_hisp_update{};
LaneEvent original_hisp_event{};
using PlayState=char(__fastcall*)(Ptr);
PlayState original_play_state{};
using HudEvent=void(__fastcall*)(Ptr,Ptr);
HudEvent original_hud_event{};
bool camera_reported{};
bool camera_updated{};
MusicSelectRoute music_select_route;
thread_local Ptr updating_world{};
thread_local bool suppress_posteffects{};
std::atomic<float> requested_rate{1.f};
std::atomic<bool> feeder_seen{};
std::atomic<bool> music_muted{};
std::atomic<float> music_envelope_gain{1.f};
std::atomic<std::uint64_t> flush_generation{};
Session session;
std::mutex taint_mutex;
std::unordered_set<Ptr> tainted_worlds;
std::unordered_set<Ptr> consumed_worlds; // game-thread only; cleared on world destruction
LARGE_INTEGER frequency{}, last_qpc{};
using Update=void(__fastcall*)(Ptr);
using Collect=bool(__fastcall*)(Ptr);
using Feed=std::uint32_t(__fastcall*)(Ptr,void*,std::uint32_t);
using RequestNext=bool(__fastcall*)(Ptr,Ptr);
Update original_world_update{}, original_destroy{}, original_timer_update{}, original_camera_update{};
post_effect::EventHandler original_posteffect_event{};
post_effect::ShakeUpdate original_posteffect_shake{};
Collect original_collect{};
Feed original_feed{};
RequestNext original_request_next{};
NativeSerialize original_serialize{};
NativeContinue original_continue{};
NativeFlow original_flow{};
autoplay::Judge original_judge{};
bool autoplay_ready{};
template<class T> T fn(Ptr rva) { return reinterpret_cast<T>(image+rva); }
float position(Ptr timer) {
    return field<float>(timer,32)+(field<std::uint8_t>(timer,40)?field<float>(timer,36):0.f);
}
void mark_tainted(Ptr world) { std::lock_guard lock(taint_mutex); tainted_worlds.insert(world); }
bool is_tainted(Ptr world) { std::lock_guard lock(taint_mutex); return tainted_worlds.contains(world); }
void reset_taint(Ptr world) { std::lock_guard lock(taint_mutex); tainted_worlds.erase(world); }
bool supported(Ptr world) {
    return supports_training_world(image,world);
}
bool transport_pause(Ptr world, bool paused) {
    __try {
        const auto timer=field<Ptr>(world,216);
        if (!timer) return false;
        const bool was_paused=field<std::uint8_t>(timer,16)==0;
        if (was_paused==paused) return true;
        fn<Update>(paused?native::addresses().timer_pause:native::addresses().timer_resume)(timer);
        if (paused && field<Ptr>(world,184))
            fn<void(__fastcall*)(Ptr,Ptr)>(native::addresses().audio_stop)(field<Ptr>(world,184),0);
        QueryPerformanceCounter(&last_qpc);
        ++flush_generation;
        log(paused?"Transport paused":"Transport resumed");
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool transport_seek(Ptr world, float target,bool seek_audio=true) {
    __try {
        const auto timer=field<Ptr>(world,216);
        if (!timer || !std::isfinite(target)) return false;
        if(updating_world==world && current_world==world) suppress_posteffects=true;
        float delta=target-position(timer);
        fn<void(__fastcall*)(Ptr,const float*)>(native::addresses().timer_seek)(timer,&delta);
        // The native seek notification invalidates DrawTimer. Commit before resetting notes.
        const auto draw=field<Ptr>(world,248);
        if (draw) field<std::uint8_t>(draw,40)=1;
        original_timer_update(timer);
        for (const auto offset:{280u,296u}) {
            const auto pool=field<Ptr>(world,offset);
            if (pool) reinterpret_cast<Update>(field<Ptr>(field<Ptr>(pool,0),24))(pool);
        }
        const auto player=field<Ptr>(world,200);
        if (player) fn<Update>(native::addresses().player_reset)(player);
        if (seek_audio && field<Ptr>(world,184))
            fn<void(__fastcall*)(Ptr,int)>(native::addresses().audio_seek)(field<Ptr>(world,184),static_cast<int>(target*1000.f));
        QueryPerformanceCounter(&last_qpc);
        if (seek_audio) ++flush_generation;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
class GameBackend final : public Backend {
public:
    Ptr world;
    explicit GameBackend(Ptr p):world(p){}
    bool gauge(const GaugeConfig& config) override {
        if(!gauge_control.set(world,config)) return false;
        if(!config.enabled) return transport_seek(world,position(field<Ptr>(world,216)),false);
        return true;
    }
    bool gauge_failed(bool& failed) override { return gauge_control.failed(world,failed); }
    bool track_complete(bool& complete) override { return native_track_complete(image,world,complete); }
    bool reset_gauge() override { return gauge_control.reset(world); }
    void release_gauge() override { if(!gauge_control.release(world)) log("Gauge restoration failed"); }
    bool lane_speed(bool locked) override {
        if(!lane_control.set_locked(world,locked)) return false;
        const bool ok=transport_seek(world,position(field<Ptr>(world,216)),false);
        log(ok?(locked?"Lane Speed fixed":"Lane Speed restored"):"Lane Speed display refresh failed");
        return ok;
    }
    void release_lane_speed() override { lane_control.release(world); }
    bool pause(bool value) override {
        if (reverse_player.owns_bus()) { reverse_player.pause(value); return transport_pause(world,true); }
        return transport_pause(world,value);
    }
    bool seek(float seconds) override {
        const bool reverse=reverse_player.owns_bus();
        const bool ok=transport_seek(world,seconds,!reverse);
        if (ok && reverse) reverse_player.seek(seconds);
        log(std::string(ok?"Seek to ":"Seek failed: ")+std::to_string(seconds)+" seconds");
        return ok;
    }
    bool rate(float value) override {
        if (!feeder_seen.load() && value!=1.f) return false;
        requested_rate=value; QueryPerformanceCounter(&last_qpc);
        if (reverse_player.owns_bus()) reverse_player.rate(value);
        log("Synchronized chart/music rate: "+std::to_string(value)); return true;
    }
    bool finish() override {
        // Resume at the end, then enter native cleanup without end-of-track presentation.
        const auto data=field<Ptr>(world,152);
        const bool ok=data && seek(field<float>(data,152)+.1f);
        return ok && prepare_training_finish(image,world);
    }
    bool reverse(bool enabled) override {
        if (!enabled) {
            if (!reverse_player.owns_bus()) return true;
            const float cursor=static_cast<float>(reverse_player.position());
            reverse_player.stop();
            return transport_seek(world,cursor);
        }
        const auto clip=reverse_asset?reverse_asset->clip.load():nullptr;
        if (!clip || !transport_pause(world,true)) return false;
        const auto audio=field<Ptr>(world,184);
        const float gain=audio?std::clamp(field<unsigned>(audio,56)/127.f,0.f,1.f):1.f;
        reverse_player.start(clip,position(field<Ptr>(world,216)),requested_rate.load(),gain);
        log("Reverse playback started; native music paused, decoded PCM drives chart clock");
        return true;
    }
    bool sync_reverse_chart(float seconds) override {
        if (std::fabs(position(field<Ptr>(world,216))-seconds)<.000001f) return true;
        return transport_seek(world,seconds,false);
    }
    bool seek_chart(float seconds) override { return transport_seek(world,seconds,false); }
    bool mute(bool value) override {
        if (music_muted.exchange(value)!=value) {
            ++flush_generation;
            log(value?"Music bus muted for training rewind":"Music bus restored after training rewind");
        }
        return true;
    }
    void music_gain(float gain) override { music_envelope_gain=gain; }
};
Track read_track(Ptr world) {
    Track t;
    t.autoplay_ready=autoplay_ready;
    t.camera_ready=camera_control.available(world);
    const char* lane_reason{};
    t.lane_speed_ready=lane_control.available(world,&lane_reason);
    t.gauge_ready=gauge_control.available(world);
    log(std::string("Lane Speed context: ")+lane_reason);
    t.native_quick_finish=field<int>(image,native::addresses().game_mode)==6;
    t.music_id=field<std::uint32_t>(world,native::world_layout().music);
    t.difficulty=field<std::uint32_t>(world,native::world_layout().music+4);
    const auto data=field<Ptr>(world,152);
    if (data) {
        t.duration=field<float>(data,152);
        t.preview=read_native_chart(field<Ptr>(data,0)==image+native::addresses().fumen_data_vtable?data:0,[](Ptr source,void* target,std::size_t size)->bool {
            __try { std::memcpy(target,reinterpret_cast<const void*>(source),size); return true; }
            __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        });
        if (t.preview->error.empty()) {
            t.buttons=static_cast<std::uint32_t>(t.preview->notes.size());
            t.lasers=static_cast<std::uint32_t>(t.preview->lasers.size());
        } else log("Chart preview unavailable: "+t.preview->error);
    }
    // Copy the selected ABI's MSVC strings; never pass them across the CRT boundary.
    const auto length=field<std::size_t>(world,native::world_layout().chart_path+16);
    const char* path=field<std::size_t>(world,native::world_layout().chart_path+24)>=16?
        field<const char*>(world,native::world_layout().chart_path):reinterpret_cast<const char*>(world+native::world_layout().chart_path);
    if (path && length<4096) t.chart.assign(path,length);
    t.info=song_catalog.lookup(t.music_id,t.difficulty,t.chart);
    // Native ThreadLoad selects the actual BGM path (including variants).
    const auto audio_length=field<std::size_t>(world,native::world_layout().audio_path+16);
    const char* audio_path=field<std::size_t>(world,native::world_layout().audio_path+24)>=16?field<const char*>(world,native::world_layout().audio_path):reinterpret_cast<const char*>(world+native::world_layout().audio_path);
    if (audio_path && audio_length && audio_length<4096) {
        const auto path_on_disk=reverse_audio_path(contents_root,std::string(audio_path,audio_length));
        if (!path_on_disk.empty()) t.reverse_audio=std::make_shared<ReverseAsset>(path_on_disk);
    }
    reverse_asset=t.reverse_audio;
    reverse_reported=false;
    return t;
}
void stop_for_native_finish(Ptr world) {
    if(suppress_native_finish(image,world,session.native_exit_locked())) return;
    if (!field<std::uint8_t>(world,native::world_layout().forced_end)) return;
    const auto state=session.snapshot();
    if (state.playing && state.track.native_quick_finish) {
        // Release pause/rate and queued loop commands before native teardown.
        // Session::end retains taint for the score and scene hooks that follow.
        GameBackend backend(world);
        session.end(backend);
        log("Premium Time native quick finish: training transport stopped; result guard retained");
    }
}
void __fastcall world_update_hook(Ptr world) {
    const bool active=field<Ptr>(world,24)==image+native::addresses().play_state;
    if (active && current_world!=world) {
        if (current_world) {
            camera_control.before_update(current_world);
            GameBackend old(current_world); session.end(old);
        }
        camera_control.reset(); camera_reported=false;
        lane_fault=lane_reported=false;
        current_world=world;
        autoplay::begin_track();
        requested_rate=1.f;
        const auto timer=field<Ptr>(world,216);
        const bool ready=supported(world) && music_select_route.valid() && feeder_seen.load() && timer &&
            field<Ptr>(timer,0)==image+native::addresses().game_timer_vtable;
        session.begin(read_track(world),ready,is_tainted(world));
        QueryPerformanceCounter(&last_qpc);
        const auto metadata=session.snapshot();
        log("Gameplay entered; music="+std::to_string(metadata.track.music_id)+
            " mode="+std::to_string(field<int>(image,native::addresses().game_mode))+
            " chart="+std::to_string(metadata.track.difficulty)+" duration="+std::to_string(metadata.track.duration)+
            " supported="+std::to_string(supported(world))+" music_select_id="+std::to_string(music_select_route.process_id)+
            " audio_ready="+std::to_string(feeder_seen.load())+
            " camera_ready="+std::to_string(metadata.track.camera_ready)+
            " lane_speed_ready="+std::to_string(metadata.track.lane_speed_ready)+
            " gauge_ready="+std::to_string(metadata.track.gauge_ready)+"; training defaults to OFF");
    }
    if (current_world==world) {
        if(!lane_control.before_update(world)) lane_fault=true;
        if (!camera_control.before_update(world) && !camera_reported) {
            camera_reported=true; session.camera_unavailable();
            log("Camera pose restore failed; camera control disabled for this track");
        }
        stop_for_native_finish(world);
    }
    const auto previous=updating_world;
    const bool previous_suppression=suppress_posteffects;
    suppress_posteffects=false;
    updating_world=world;
    camera_updated=false;
    const auto control=session.snapshot();
    const bool automatic=active && world==current_world && control.playing && control.enabled && control.autoplay &&
        !control.reversing && !control.rewinding && supported(world);
    if (automatic) mark_tainted(world);
    if (active && world==current_world && autoplay_ready) autoplay::update(world,automatic,original_world_update);
    else original_world_update(world);
    updating_world=previous;
    suppress_posteffects=previous_suppression;
    if (current_world==world) {
        if(lane_fault && !lane_reported) {
            lane_reported=true;
            GameBackend backend(world); backend.reverse(false); backend.pause(true); backend.release_lane_speed();
            session.lane_speed_unavailable();
            log("Lane Speed context failed; paused with results blocked");
        }
        if (session.camera_requested() && !camera_updated && !camera_reported) {
            camera_reported=true; session.camera_unavailable();
            log("Camera updater did not run; camera control disabled for this track");
        }
    }
    if (current_world==world && field<Ptr>(world,24)!=image+native::addresses().play_state) {
        // Stop transporting once the native state machine leaves active play.
        GameBackend backend(world); session.end(backend);
        requested_rate=1.f;
        current_world=0;
    }
}
char __fastcall play_state_hook(Ptr world) {
    // The timer already ran this frame. Intercept here before native play state
    // checks all-failed, adds clear time or queues its failure/auto-retry scenes.
    if(world==current_world) suppress_native_finish(image,world,session.native_exit_locked());
    if(world==current_world && !field<std::uint8_t>(world,native::world_layout().forced_end) && supported(world)) {
        GameBackend backend(world);
        if(session.intercept_gauge_failure(position(field<Ptr>(world,216)),backend)) return 0;
        if(session.intercept_track_end(position(field<Ptr>(world,216)),backend)) return 0;
        if(session.native_exit_locked() && !session.snapshot().ready) return 0;
    }
    return original_play_state(world);
}
void __fastcall hud_event_hook(Ptr hud,Ptr events) {
    original_hud_event(hud,events);
    if(!gauge_control.after_events(hud,events)) log("Gauge HUD refresh failed");
}
bool suppress_posteffect_update(Ptr world) {
    if (!world || world!=current_world) return false;
    if (suppress_posteffects) return true;
    if (!is_tainted(world)) return false;
    const auto timer=field<Ptr>(world,216);
    return timer && field<std::uint8_t>(timer,16)==0;
}
Ptr __fastcall posteffect_event_hook(Ptr effect,Ptr events) {
    return post_effect::dispatch(original_posteffect_event,effect,events,suppress_posteffect_update(updating_world));
}
bool __fastcall posteffect_shake_hook(Ptr effect,const float* time) {
    const auto world=updating_world;
    if (!world || world!=current_world || (!suppress_posteffects && !is_tainted(world)))
        return original_posteffect_shake(effect,time);
    return post_effect::update_shake(original_posteffect_shake,effect,time,suppress_posteffect_update(world));
}
void __fastcall camera_update_hook(Ptr updater) {
    original_camera_update(updater);
    const auto world=updating_world;
    // The same updater is also used by previews/other worlds. Match its renderer.
    if (!world || world!=current_world || field<Ptr>(updater,24)!=field<Ptr>(world,native::world_layout().renderer)) return;
    camera_updated=true;
    const bool locked=field<Ptr>(world,24)==image+native::addresses().play_state &&
        session.camera_requested() && supported(world);
    // Native initialization registers AngleUpdater, then CameraUpdater, then the
    // lane/cursor/bomb views. Lock here before those views generate vertices.
    if (!camera_control.after_update(world,locked) && !camera_reported) {
        camera_reported=true; session.camera_unavailable();
        log("Camera pose capture failed; camera control disabled for this track");
    }
}
Ptr __fastcall timing_constructor_hook(Ptr output,Ptr input,bool fixed) {
    return lane_control.construct_timing(original_timing_constructor,output,input,fixed);
}
Ptr __fastcall hisp_update_hook(Ptr updater) {
    const bool owned=lane_control.matches_updater(updater,false);
    if(owned && !lane_control.before_update(current_world)) lane_fault=true;
    const Ptr result=original_hisp_update(updater);
    if(owned && !lane_control.after_update(current_world)) lane_fault=true;
    return result;
}
Ptr __fastcall hisp_event_hook(Ptr updater,Ptr events) {
    const bool owned=lane_control.matches_updater(updater,true);
    if(owned && !lane_control.before_update(current_world)) lane_fault=true;
    const Ptr result=original_hisp_event(updater,events);
    if(owned && !lane_control.after_update(current_world)) lane_fault=true;
    return result;
}
Ptr __fastcall judge_hook(Ptr referee,Ptr output,Ptr note,float time) {
    return autoplay::judge(original_judge,referee,output,note,time);
}
void __fastcall timer_update_hook(Ptr timer) {
    const auto world=updating_world;
    if (!world || world!=current_world || field<Ptr>(world,216)!=timer) {
        original_timer_update(timer); return;
    }
    stop_for_native_finish(world);
    if (reverse_asset && !reverse_reported && reverse_asset->finished.load(std::memory_order_acquire)) {
        const auto clip=reverse_asset->clip.load();
        log(clip?"Reverse audio ready: "+std::to_string(clip->duration())+" seconds":
                 "Reverse audio unavailable: "+reverse_asset->error);
        reverse_reported=true;
    }
    const auto state=session.snapshot();
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    const double elapsed=last_qpc.QuadPart?static_cast<double>(now.QuadPart-last_qpc.QuadPart)/static_cast<double>(frequency.QuadPart):0.;
    if (state.enabled && field<std::uint8_t>(timer,16) && last_qpc.QuadPart) {
        // Original code samples QPC itself. Correct its accumulated duration before
        // it updates the public float clock and invokes all five listener groups.
        field<std::int64_t>(timer,24)+=static_cast<std::int64_t>(elapsed*(state.rate-1.f)*1e9);
        const auto draw=field<Ptr>(world,248);
        if (draw && field<Ptr>(draw,0)==image+native::addresses().draw_timer_vtable)
            field<std::uint8_t>(draw,40)=1; // Disable its real-time extrapolation for this frame.
    }
    last_qpc=now;
    original_timer_update(timer);
    GameBackend backend(world);
    const float cursor=state.reversing && reverse_player.owns_bus()?static_cast<float>(reverse_player.position()):position(timer);
    session.tick(cursor,field<std::uint8_t>(timer,16)==0,backend,static_cast<float>(elapsed));
    if (session.suppress_result()) mark_tainted(world);
}
void __fastcall destroy_hook(Ptr world) {
    if (current_world==world) {
        camera_control.before_update(world); camera_control.reset();
        GameBackend backend(world); session.end(backend);
        current_world=0; requested_rate=1.f;
    }
    original_destroy(world);
    reset_taint(world);
    consumed_worlds.erase(world);
}
bool __fastcall request_next_hook(Ptr scene,Ptr argument) {
    const auto table=field<Ptr>(scene,0);
    if (table==image+native::addresses().music_select_vtable) {
        music_select_route=capture_music_select_route(scene);
        log("Captured music-select route: scene="+std::to_string(music_select_route.process_id)+
            " next="+std::to_string(music_select_route.next_id)+" parent="+std::to_string(music_select_route.parent_id));
    }
    if (table==image+native::addresses().alternative_scene_vtable) {
        const auto world=field<Ptr>(scene,416);
        if(world && is_tainted(world) && session.native_exit_locked()) return false;
        if (world && is_tainted(world) && music_select_route.valid()) {
            const bool accepted=request_music_select(scene,music_select_route,original_request_next);
            log(accepted?"Practice exit: native parent-flow request accepted; result scene skipped":
                         "Practice exit: native parent-flow request rejected; result remains blocked");
            return accepted;
        }
    }
    return original_request_next(scene,argument);
}
bool __fastcall collect_hook(Ptr world) {
    if (is_tainted(world)) {
        if(session.native_exit_locked()) { session.result_blocked(); return false; }
        if (!consumed_worlds.contains(world)) {
            const auto data=fn<Ptr(__fastcall*)()>(native::addresses().stage_data)();
            alignas(16) std::array<std::byte,236> options{};
            fn<Ptr(__fastcall*)(Ptr,Ptr)>(native::addresses().track_options)(reinterpret_cast<Ptr>(options.data()),world+native::world_layout().music);
            PracticeStageRecord record(static_cast<unsigned>(field<int>(data,32)),field<unsigned>(world,native::world_layout().music),
                                       field<unsigned>(world,native::world_layout().music+4),options);
            if (!append_practice_stage(image,data,record)) {
                log("Practice stage bookkeeping failed; score collection remains blocked");
                return false;
            }
            if (!remember_practice_selection(image,world))
                log("Practice selection memory unavailable; score collection remains blocked");
            consumed_worlds.insert(world);
            log("Practice consumed stage "+std::to_string(field<int>(data,32))+"; no scored result collected");
        }
        session.result_blocked();
        log("Practice score/stat/reward collection blocked; bookkeeping excluded from upload");
        return false;
    }
    return original_collect(world);
}
std::int64_t __fastcall serialize_hook(Ptr data,Ptr xml) {
    return serialize_practice_safe(image,data,xml,original_serialize);
}
bool __fastcall continue_hook(Ptr data) { return can_continue_practice_safe(image,data,original_continue); }
unsigned __fastcall flow_hook(Ptr flow,int state,Ptr argument) {
    return decide_practice_flow(image,flow,state,argument,original_continue,original_flow);
}
// Runs on the native audio thread, immediately before the vocoder reads its state.
void flush_vocoder(Ptr feeder) {
    const auto begin=field<Ptr>(feeder,32800), end=field<Ptr>(feeder,32808);
    if (end<begin || end-begin>32) return;
    for (auto i=begin;i!=end;i+=16) {
        const auto p=field<Ptr>(i,0);
        field<Ptr>(p,248)=field<Ptr>(p,240);
        field<std::uint64_t>(p,24)=field<std::uint64_t>(p,32)=0;
        field<int>(p,232)=0;
        field<Ptr>(p,152)=field<Ptr>(p,144);
        field<Ptr>(p,200)=field<Ptr>(p,192);
        field<Ptr>(p,176)=field<Ptr>(p,168);
    }
}
std::uint32_t __fastcall feed_hook(Ptr feeder,void* out,std::uint32_t size) {
    // This build constructs one TimeStretchAudioFeeder for the music bus.
    thread_local Ptr seen_feeder{};
    thread_local float applied=1.f;
    thread_local std::uint64_t flushed{};
    thread_local AudioEnvelope envelope;
    feeder_seen=true;
    if (music_muted.load() && out) {
        std::memset(out,0,size);
        envelope.reset(0.f);
        return size;
    }
    if (out && size%8==0 && reverse_player.render({static_cast<float*>(out),size/sizeof(float)})) {
        envelope.apply({static_cast<float*>(out),size/sizeof(float)},music_envelope_gain.load());
        return size;
    }
    if (seen_feeder!=feeder) { seen_feeder=feeder; applied=1.f; flushed=flush_generation.load(); }
    const auto generation=flush_generation.load();
    if (flushed!=generation) { flush_vocoder(feeder); flushed=generation; }
    const float desired=requested_rate.load();
    if (desired!=applied) {
        fn<void(__fastcall*)(Ptr,float)>(native::addresses().audio_rate)(feeder,desired);
        applied=desired;
    }
    const auto written=original_feed(feeder,out,size);
    if(out && written<=size && written%8==0)
        envelope.apply({static_cast<float*>(out),written/sizeof(float)},music_envelope_gain.load());
    return written;
}
}
Session& game_session() { return session; }
bool install_game_hooks(HMODULE module,std::string& reason) {
    if (!native::validate(module,reason)) return false;
    log(reason);
    for (const auto& detail:native::resolution().details) log("Native resolver: "+detail);
    image=reinterpret_cast<Ptr>(module);
    wchar_t module_path[32768]{};
    if (const auto length=GetModuleFileNameW(module,module_path,static_cast<DWORD>(std::size(module_path)));
        length && length<std::size(module_path)) {
        std::string catalog_error;
        contents_root=std::filesystem::path(module_path).parent_path().parent_path();
        if (song_catalog.load(contents_root,catalog_error))
            log("Song catalog loaded: "+std::to_string(song_catalog.size())+" songs");
        else log("Song information unavailable: "+catalog_error);
    }
    const auto discovered=autoplay_discovery::resolve(module);
    const auto& a=native::addresses();
    camera_control.configure({image+a.world_vtable,image+a.renderer_vtable,image+a.camera_engine_vtable,
        image+a.camera_world_vtable,image+a.camera_space_vtable,
        fn<camera::Rebuild>(a.camera_rebuild_space),fn<camera::SetView>(a.camera_set_view),native::world_layout()});
    log("Camera pose control located; native animations continue behind the locked view");
    lane_control.configure({image+a.world_vtable,image+a.game_context_vtable,image+a.fumen_player_vtable,
        image+a.draw_timer_vtable,image+a.fumen_data_vtable,image+a.lane_range_vtable,
        fn<lane_speed::Build>(a.lane_build_notes),fn<lane_speed::Load>(a.lane_load_notes),fn<lane_speed::Destroy>(a.lane_destroy_notes),native::world_layout()});
    log("Lane Speed display timing and native note rebuild located");
    gauge_control.configure({image+a.world_vtable,image+a.fumen_player_vtable,image+a.fumen_data_vtable,image+a.gauge_ref_vtable,
        fn<gauge::Allocate>(a.gauge_allocate),fn<gauge::Push>(a.gauge_type_push),fn<gauge::Build>(a.gauge_build_stack),
        fn<gauge::Destroy>(a.gauge_destroy_player),fn<gauge::Destroy>(a.gauge_destroy_types),
        a.gauge_chart_options?fn<gauge::Options>(a.gauge_chart_options):nullptr,
        fn<gauge::Refresh>(a.gauge_hud_refresh),fn<gauge::Failed>(a.gauge_failed),
        native::world_layout(),native::resolution().chart_gauge_gain});
    log("Native Gauge switching and ARS stacks located");
    autoplay_ready=discovered && discovered.layout.update==image+a.world_update &&
        discovered.layout.world_vtable==image+a.world_vtable && discovered.layout.timer_vtable==image+a.game_timer_vtable;
    if (autoplay_ready) {
        autoplay::configure(discovered.layout);
        log("Native AutoPlay located; AutoJudge RVA="+std::to_string(discovered.layout.judge-image));
    } else log(std::string("Native AutoPlay unavailable; training remains available: ")+
               (discovered?"resolver disagrees with training world/timer":discovered.error));
    QueryPerformanceFrequency(&frequency);
    struct Hook { Ptr rva; void* detour; void** original; };
    std::vector<Hook> hooks{
        {native::addresses().serialize_result,reinterpret_cast<void*>(serialize_hook),reinterpret_cast<void**>(&original_serialize)},
        {native::addresses().can_continue,reinterpret_cast<void*>(continue_hook),reinterpret_cast<void**>(&original_continue)},
        {native::addresses().flow_decision,reinterpret_cast<void*>(flow_hook),reinterpret_cast<void**>(&original_flow)},
        {native::addresses().collect_result,reinterpret_cast<void*>(collect_hook),reinterpret_cast<void**>(&original_collect)},
        {native::addresses().world_destroy,reinterpret_cast<void*>(destroy_hook),reinterpret_cast<void**>(&original_destroy)},
        {native::addresses().timer_update,reinterpret_cast<void*>(timer_update_hook),reinterpret_cast<void**>(&original_timer_update)},
        {native::addresses().audio_feed,reinterpret_cast<void*>(feed_hook),reinterpret_cast<void**>(&original_feed)},
        {native::addresses().request_next,reinterpret_cast<void*>(request_next_hook),reinterpret_cast<void**>(&original_request_next)},
        {native::addresses().camera_update,reinterpret_cast<void*>(camera_update_hook),reinterpret_cast<void**>(&original_camera_update)},
        {a.posteffect_event,reinterpret_cast<void*>(posteffect_event_hook),reinterpret_cast<void**>(&original_posteffect_event)},
        {a.posteffect_shake,reinterpret_cast<void*>(posteffect_shake_hook),reinterpret_cast<void**>(&original_posteffect_shake)},
        {a.lane_timing_constructor,reinterpret_cast<void*>(timing_constructor_hook),reinterpret_cast<void**>(&original_timing_constructor)},
        {a.hisp_update,reinterpret_cast<void*>(hisp_update_hook),reinterpret_cast<void**>(&original_hisp_update)},
        {a.play_state,reinterpret_cast<void*>(play_state_hook),reinterpret_cast<void**>(&original_play_state)},
        {a.gauge_hud_event,reinterpret_cast<void*>(hud_event_hook),reinterpret_cast<void**>(&original_hud_event)},
        {a.hisp_event,reinterpret_cast<void*>(hisp_event_hook),reinterpret_cast<void**>(&original_hisp_event)},
        {native::addresses().world_update,reinterpret_cast<void*>(world_update_hook),reinterpret_cast<void**>(&original_world_update)}
    };
    if (autoplay_ready) hooks.push_back({discovered.layout.judge-image,reinterpret_cast<void*>(judge_hook),reinterpret_cast<void**>(&original_judge)});
    std::vector<void*> created;
    for (const auto& h:hooks) {
        auto* address=reinterpret_cast<void*>(image+h.rva);
        const auto status=MH_CreateHook(address,h.detour,h.original);
        if (status!=MH_OK) {
            for (auto* p:created) MH_RemoveHook(p);
            reason=std::string("Hook creation failed: ")+MH_StatusToString(status); return false;
        }
        created.push_back(address);
    }
    for (auto* p:created) MH_QueueEnableHook(p);
    const auto status=MH_ApplyQueued();
    if (status!=MH_OK) {
        for (auto* p:created) { MH_DisableHook(p); MH_RemoveHook(p); }
        reason=std::string("Hook activation failed: ")+MH_StatusToString(status); return false;
    }
    reason="Game hooks ready; configured hotkey opens overlay";
    log(reason);
    return true;
}
}
