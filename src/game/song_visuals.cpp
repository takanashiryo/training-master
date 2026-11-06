#include "game/song_visuals.hpp"
#include <windows.h>
#include <algorithm>
#include <stdexcept>

namespace training::song_visuals {
namespace {
// Clang SEH cannot catch a faulting load in the handler's own frame. Keep
// native reads in a separate frame, including optimized LLVM MinGW builds.
template<class T> __declspec(noinline) T load(Ptr p) {
    return *reinterpret_cast<const T*>(p);
}
template<class T> T read(Ptr p,unsigned offset=0) {
    T value{};
    __try { value=load<T>(p+offset); }
    __except(EXCEPTION_EXECUTE_HANDLER) { throw std::runtime_error("Song visual context is unreadable"); }
    return value;
}
template<class F> void array(Ptr object,unsigned offset,unsigned stride,F visit) {
    const auto begin=read<Ptr>(object,offset),end=read<Ptr>(object,offset+8);
    if(end<begin || (end-begin)%stride || (end-begin)/stride>4096)
        throw std::runtime_error("Invalid song visual array at offset "+std::to_string(offset));
    for(auto p=begin;p<end;p+=stride) visit(p);
}
bool has(const std::vector<Ptr>& values,Ptr p) { return std::find(values.begin(),values.end(),p)!=values.end(); }
bool restore_byte(Ptr object,unsigned offset,unsigned char value) {
    __try {
        *reinterpret_cast<unsigned char*>(object+offset)=value;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool set_visibility(SetVisible set,Ptr component,bool value) {
    __try { set(component,value); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool get_visibility(Visible get,Ptr layer,bool& saved) {
    __try {
        saved=get(layer);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool send_event(Event handler,Ptr recipient,Ptr range) {
    __try { handler(recipient,range); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct Bounds { Ptr begin,end,capacity; };
Bounds shader_bounds(Ptr effect) {
    const auto bounds=read<Bounds>(effect,176);
    const auto output_begin=read<Ptr>(effect,152),output_end=read<Ptr>(effect,160);
    if(bounds.end<bounds.begin || (bounds.end-bounds.begin)%16 || bounds.capacity<bounds.end ||
       (bounds.end-bounds.begin)/16>4096 || output_end<output_begin || (output_end-output_begin)%16 ||
       output_end-output_begin<bounds.end-bounds.begin)
        throw std::runtime_error("Invalid post-effect shader/output arrays");
    return bounds;
}
bool render_bypass(PostRender original,Ptr effect,const Bounds& saved,Ptr begin,Ptr end) {
    auto* bounds=reinterpret_cast<Bounds*>(effect+176);
    bool borrowed=false;
    __try {
        *bounds={begin,end,end}; borrowed=true;
        return original(effect);
    } __finally {
        if(borrowed) *bounds=saved;
    }
}
}
void Controller::hide_sprite(Ptr layer) {
    if(!layer || std::any_of(sprites_.begin(),sprites_.end(),[&](auto s){return s.owner==layer;})) return;
    // GameExBgView constructs these roots as CLayer, independently of the
    // ordinary GameBackgroundView/Autopara background and post output layers.
    if(read<Ptr>(layer)!=layout_.layer) throw std::runtime_error("Unexpected extra sprite layer interface");
    const auto handle=read<unsigned>(layer,8);
    // Unloaded roots have no native BM2D object to hide yet.
    if(static_cast<int>(handle)<=0) return;
    bool visible{};
    // Record ownership before mutating, so a failing native setter rolls back.
    if(!get_visibility(layout_.visible,layer,visible))
        throw std::runtime_error("Cannot read extra sprite visibility");
    sprites_.push_back({layer,handle,visible});
    if(!set_visibility(layout_.set_visible,layer,false))
        throw std::runtime_error("Cannot hide extra sprite layer");
}
void Controller::hide_live2d(Ptr component) {
    if(!component) return;
    if(std::any_of(live2d_.begin(),live2d_.end(),[&](auto c){return c.component==component;})) return;
    if(read<Ptr>(component)!=layout_.live2d || read<unsigned char>(component,44)>1)
        throw std::runtime_error("Unexpected Live2D component interface");
    // Native CLive2DCrewComponent::SetVisible (slot 8) writes +44, then
    // propagates to each LAppModel's +573 flag through 64-byte model entries.
    // Preserve per-model visibility too: not all loaded models need be shown.
    array(component,16,64,[&](Ptr entry) {
        const auto instance=read<Ptr>(entry,8);
        const auto model=instance?read<Ptr>(instance,8):0;
        if(!model || read<unsigned char>(model,573)>1) throw std::runtime_error("Invalid Live2D model");
        if(std::none_of(models_.begin(),models_.end(),[&](auto m){return m.model==model;}))
            models_.push_back({model,read<Ptr>(model),read<unsigned char>(model,573)});
    });
    live2d_.push_back({component,read<unsigned char>(component,44)});
    if(!set_visibility(layout_.set_live2d_visible,component,false))
        throw std::runtime_error("Cannot hide Live2D component");
}
void Controller::collect(Ptr view) {
    if(!view || has(views_,view)) return;
    const auto table=read<Ptr>(view);
    if(table==layout_.ex_bg) {
        views_.push_back(view);
        // The main GameBackgroundView and Autopara scenery are separate views.
        // ExBg owns extra overlays (e.g. mixxion's bg_mixxion_eff fragments and
        // cutin); suppress their drawing without stopping their native updates.
        stage_="extra sprite layers";
        array(view,layout_.sprites.array,layout_.sprites.stride,[&](Ptr entry) {
            hide_sprite(read<Ptr>(entry,layout_.sprites.layer));
        });
        stage_="Live2D actors";
        array(view,624,16,[&](Ptr entry) {
            const auto actor=read<Ptr>(entry);
            if(actor) hide_live2d(read<Ptr>(actor,16));
        });
        // Engine-backed actors (including animated characters) draw separately.
        // Their draw callback is suppressed by hides_view rather than stopped.
    } else if(table==layout_.post) {
        stage_="post-effect render pipeline";
        array(view,64,16,[&](Ptr item) {
            const auto processor=read<Ptr>(item);
            if(!processor) return;
            const auto effect=read<Ptr>(processor,232);
            if(effect) {
                if(read<Ptr>(effect)!=layout_.priority) throw std::runtime_error("Unexpected song post-effect interface");
                if(!hides_effect(effect)) {
                    const auto bounds=shader_bounds(effect);
                    bool animated_output=false;
                    array(processor,40,80,[&](Ptr output) {
                        const auto layer=read<Ptr>(output);
                        if(layer) { hide_sprite(layer); animated_output=true; }
                    });
                    effects_.push_back({effect,animated_output?processor:0,std::vector<Ptr>((bounds.end-bounds.begin)/sizeof(Ptr),0)});
                }
            }
        });
    }
}
bool Controller::apply(Ptr world,bool hide) {
    restore();
    error_.clear(); stage_="world views";
    if(!hide) return true;
    if(!world || !layout_) { error_="Song visual layout is not verified"; return false; }
    try {
        for(unsigned offset:{440u,464u}) array(world,offset,16,[&](Ptr entry){stage_="world views"; collect(read<Ptr>(entry));});
        return true;
    } catch(const std::exception& e) { error_=std::string(stage_)+": "+e.what(); }
    catch(...) { error_=std::string(stage_)+": unexpected native failure"; }
    restore(); return false;
}
void Controller::restore() {
    for(auto s:sprites_) {
        try {
            if(read<Ptr>(s.owner)==layout_.layer && read<unsigned>(s.owner,8)==s.handle)
                set_visibility(layout_.set_visible,s.owner,s.visible);
        } catch(...) {}
    }
    sprites_.clear();
    for(auto c:live2d_) {
        try {
            if(read<Ptr>(c.component)==layout_.live2d)
                set_visibility(layout_.set_live2d_visible,c.component,c.visible!=0);
        } catch(...) {}
    }
    live2d_.clear();
    for(auto m:models_) {
        try {
            if(read<Ptr>(m.model)==m.table) restore_byte(m.model,573,m.visible);
        } catch(...) {}
    }
    models_.clear();
    views_.clear(); effects_.clear();
}
bool Controller::hides_view(Ptr view) const { return has(views_,view); }
bool Controller::transport(Ptr world,bool seeking,Event post_original,std::uint64_t now_ms) {
    constexpr std::uint64_t settle_ms=150;
    if(reset_world_!=world) {
        reset_world_=world; interrupted_=dirty_=false;
    }
    const bool recent=interrupted_ && now_ms>=last_seek_ms_ && now_ms-last_seek_ms_<settle_ms;
    if(seeking) {
        last_seek_ms_=now_ms;
        // Reverse playback and slider dragging call seek each frame. Native
        // Reset reconstructs GPU resources: never do that for each sample.
        if(interrupted_) return true;
    } else {
        if(!interrupted_ || recent) return true;
        // End a navigation burst. Clean up any effects triggered while moving
        // once, before this frame's native event delivery resumes.
        interrupted_=false;
        if(!dirty_) return true;
    }
    restore();
    error_.clear();
    if(!world || !layout_.can_reset() || !post_original) {
        error_="Song animation reset layout is not verified"; return false;
    }
    try {
        // Collect and validate all recipients before dispatching anything.
        std::vector<std::pair<Ptr,Event>> recipients;
        for(unsigned offset:{440u,464u}) array(world,offset,16,[&](Ptr entry) {
            const auto view=read<Ptr>(entry);
            if(!view) return;
            const auto table=read<Ptr>(view);
            if(table!=layout_.post) return;
            if(read<Ptr>(view,8)!=layout_.post_events) throw std::runtime_error("Unexpected post-effect event interface");
            if(std::none_of(recipients.begin(),recipients.end(),[&](auto r){return r.first==view+8;}))
                recipients.emplace_back(view+8,post_original);
        });
        // Native handlers consume only the tag for Reset (35). No variant with
        // owning strings/shared_ptrs is constructed, copied or destroyed here.
        alignas(8) unsigned char event[272]{};
        const unsigned reset=35; std::memcpy(event,&reset,sizeof(reset));
        const auto begin=reinterpret_cast<Ptr>(event);
        Ptr range[]{begin,begin+sizeof(event),begin+sizeof(event)};
        for(auto [recipient,handler]:recipients)
            if(!send_event(handler,recipient,reinterpret_cast<Ptr>(range)))
                throw std::runtime_error("Native animation reset failed");
        interrupted_=seeking; dirty_=false;
        return true;
    } catch(const std::exception& e) { error_=e.what(); }
    return false;
}
void Controller::post_events(Ptr events) {
    if(!events) return;
    if(!interrupted_) return;
    // Commands may recreate effects during navigation. Remember the cleanup
    // obligation, without forcing the next per-frame seek to rebuild again.
    try {
        array(events,0,272,[&](Ptr event) {
            if(read<unsigned>(event)==40) dirty_=true;
        });
    } catch(...) { dirty_=true; }
}
bool Controller::hides_effect(Ptr effect) const {
    return std::any_of(effects_.begin(),effects_.end(),[&](const auto& e){return e.owner==effect;});
}
bool Controller::render(PostRender original,Ptr effect) {
    const auto entry=std::find_if(effects_.begin(),effects_.end(),[&](const auto& e){return e.owner==effect;});
    if(entry==effects_.end()) return original(effect);
    Bounds bounds{};
    try {
        bounds=shader_bounds(effect);
        entry->bypass.assign((bounds.end-bounds.begin)/sizeof(Ptr),0);
    } catch(...) { return original(effect); }
    // Keep one empty shader slot per native output slot. Native rendering then
    // refreshes every output with this frame's unfiltered capture, still runs
    // composition callbacks, and balances texture ownership. An empty list or a
    // skipped callback would leave the cached gameplay frame stale.
    const auto begin=reinterpret_cast<Ptr>(entry->bypass.data());
    const bool rendered=entry->bypass.empty()?original(effect):
        render_bypass(original,effect,bounds,begin,begin+entry->bypass.size()*sizeof(Ptr));
    // Animated BM2D output roots can shatter/move even with all shaders
    // disabled. At the native capture-end callback, present the refreshed raw
    // output with the engine's full-screen path instead of those geometry roots.
    // Capture, texture rebinding and all callbacks above still run each frame.
    if(rendered && entry->processor) return layout_.present(entry->processor);
    return rendered;
}
}
