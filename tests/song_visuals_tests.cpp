#include "game/song_visuals.hpp"
#include "game/autoplay_resolver.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace training::song_visuals;
namespace {
template<std::size_t N> struct Bytes {
    alignas(8) std::array<unsigned char,N> data{};
    Ptr ptr() { return reinterpret_cast<Ptr>(data.data()); }
    template<class T> void put(unsigned offset,T value) { std::memcpy(data.data()+offset,&value,sizeof(value)); }
};
struct Layer { Ptr table{4}; unsigned handle{1}; bool shown{true}; };
bool __fastcall visible(Ptr p) { return reinterpret_cast<Layer*>(p)->shown; }
void __fastcall set_visible(Ptr p,bool value) { reinterpret_cast<Layer*>(p)->shown=value; }
void __fastcall set_live2d_visible(Ptr p,bool value) { *reinterpret_cast<unsigned char*>(p+44)=value?1:0; }
bool __fastcall present_stub(Ptr) { return true; }
void check(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
struct Texture { Ptr table; int frame{},references{}; };
struct Buffer { Ptr unused{},texture{}; };
int displayed_frame=-1,shader_calls{},compositions{};
const Layer* composition_surface{};
void __fastcall retain_texture(Ptr p) { ++reinterpret_cast<Texture*>(p)->references; }
void __fastcall release_texture(Ptr p) { --reinterpret_cast<Texture*>(p)->references; }
bool __fastcall shader_apply(Ptr,Ptr) { ++shader_calls; return false; }
void __fastcall compose_frame(Ptr,Ptr args) {
    const auto effect=*reinterpret_cast<const Ptr*>(args);
    const auto outputs=*reinterpret_cast<const Ptr*>(effect+152);
    const auto buffer=*reinterpret_cast<const Ptr*>(outputs);
    if(!composition_surface || composition_surface->shown)
        displayed_frame=reinterpret_cast<const Texture*>(reinterpret_cast<const Buffer*>(buffer)->texture)->frame;
    ++compositions;
}
int raw_presentations{};
bool __fastcall present_frame(Ptr processor) {
    const auto effect=*reinterpret_cast<const Ptr*>(processor+232);
    const auto outputs=*reinterpret_cast<const Ptr*>(effect+152);
    const auto buffer=*reinterpret_cast<const Ptr*>(outputs);
    displayed_frame=reinterpret_cast<const Texture*>(reinterpret_cast<const Buffer*>(buffer)->texture)->frame;
    ++raw_presentations;
    return true;
}
bool __fastcall faulting_render(Ptr) { RaiseException(0xe0421938,0,0,nullptr); return false; }
bool render_fault_restores(Controller& controller,Ptr effect) {
    __try { controller.render(faulting_render,effect); }
    __except(GetExceptionCode()==0xe0421938?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
bool __fastcall throwing_render(Ptr) { throw std::runtime_error("render callback failure fixture"); }
void sprite_effect_regression() {
    Layout layout{1,2,3,4,5,6,7,8,visible,set_visible};
    layout.live2d=9; layout.set_live2d_visible=set_live2d_visible;
    layout.present=present_stub;
    Controller controller; controller.configure(layout);
    Bytes<512> world;
    Bytes<736> extras;
    Bytes<448> glass_entry;
    Layer glass,background,lane,hud;
    extras.put(0,layout.ex_bg);
    // mixxion's bg_mixxion_eff/cutin are extra BM2D CLayer roots, loaded
    // from 304-byte definitions into 448-byte runtime entries. Its ordinary
    // GameBackgroundView and Autopara background are separate world views.
    glass_entry.put(304,reinterpret_cast<Ptr>(&glass));
    extras.put(536,glass_entry.ptr()); extras.put(544,glass_entry.ptr()+448);
    Ptr views[]{extras.ptr(),0,reinterpret_cast<Ptr>(&background),0,
                reinterpret_cast<Ptr>(&lane),0,reinterpret_cast<Ptr>(&hud),0};
    world.put(440,reinterpret_cast<Ptr>(views)); world.put(448,reinterpret_cast<Ptr>(views)+sizeof(views));
    check(controller.apply(world.ptr(),true),"extra sprite effect fixture must be supported");
    check(!glass.shown && background.shown && lane.shown && hud.shown,
          "glass-fragment sprite roots must hide while the base background, lane and HUD stay visible");
    controller.restore();
    check(glass.shown,"clearing hide mode must restore extra sprite effects");
}
void native_render_regression(HMODULE module) {
    const auto discovered=training::autoplay_discovery::resolve(module);
    check(discovered && discovered.layout.song_visuals,"native presentation render discovery");
    auto layout=discovered.layout.song_visuals;
    layout.visible=visible; layout.set_visible=set_visible;
    check(layout.present!=nullptr,"native raw presentation discovery");
    layout.present=present_frame;
    const auto render=reinterpret_cast<PostRender>(layout.post_render);
    Bytes<512> world;
    Bytes<96> post;
    Bytes<248> processor;
    Bytes<224> effect;
    Bytes<80> output_entry;
    Layer output_surface{layout.layer,1,true};
    composition_surface=&output_surface;
    Ptr texture_table[]{0,reinterpret_cast<Ptr>(retain_texture),reinterpret_cast<Ptr>(release_texture)};
    Texture input{reinterpret_cast<Ptr>(texture_table)},stale{reinterpret_cast<Ptr>(texture_table),-1};
    Buffer input_buffer{0,reinterpret_cast<Ptr>(&input)},stale_buffer{0,reinterpret_cast<Ptr>(&stale)};
    Ptr shader_table[]{0,0,0,reinterpret_cast<Ptr>(shader_apply),0};
    Ptr shader=reinterpret_cast<Ptr>(shader_table);
    Ptr shaders[]{reinterpret_cast<Ptr>(&shader),0,reinterpret_cast<Ptr>(&shader),0};
    Ptr outputs[]{reinterpret_cast<Ptr>(&stale_buffer),0,reinterpret_cast<Ptr>(&stale_buffer),0};
    Ptr callback_table[]{0,0,reinterpret_cast<Ptr>(compose_frame)};
    Ptr callback=reinterpret_cast<Ptr>(callback_table);
    Bytes<64> callback_entry;
    callback_entry.put(56,reinterpret_cast<Ptr>(&callback));
    effect.put(0,layout.priority); effect.put(16,layout.target); effect.put<unsigned char>(24,1);
    effect.put(48,reinterpret_cast<Ptr>(&input)); effect.put(112,reinterpret_cast<Ptr>(&input_buffer));
    effect.put(152,reinterpret_cast<Ptr>(outputs)); effect.put(160,reinterpret_cast<Ptr>(outputs)+sizeof(outputs));
    effect.put(176,reinterpret_cast<Ptr>(shaders)); effect.put(184,reinterpret_cast<Ptr>(shaders)+sizeof(shaders));
    effect.put(192,reinterpret_cast<Ptr>(shaders)+sizeof(shaders));
    effect.put(200,callback_entry.ptr()); effect.put(208,callback_entry.ptr()+64);
    processor.put(232,effect.ptr()); processor.put<unsigned char>(84,1);
    processor.put(40,output_entry.ptr()); processor.put(48,output_entry.ptr()+80);
    output_entry.put(0,reinterpret_cast<Ptr>(&output_surface));
    Ptr processors[]{processor.ptr(),0},views[]{post.ptr(),0};
    post.put(0,layout.post); post.put(64,reinterpret_cast<Ptr>(processors)); post.put(72,reinterpret_cast<Ptr>(processors)+sizeof(processors));
    world.put(440,reinterpret_cast<Ptr>(views)); world.put(448,reinterpret_cast<Ptr>(views)+sizeof(views));
    Controller controller; controller.configure(layout);
    displayed_frame=-1; shader_calls=compositions=raw_presentations=0;
    check(controller.apply(world.ptr(),false) && controller.render(render,effect.ptr()),"normal native render must execute");
    check(displayed_frame==0 && shader_calls==2 && compositions==1,"native fixture must draw a live frame through its shader slots");
    for(int frame=1;frame<=120;++frame) {
        check(controller.apply(world.ptr(),true),"hide mode must accept a live render pipeline");
        // CPriorityRangeRenderTarget's begin/end callbacks use this enable flag.
        if(effect.data[24]) input.frame=frame;
        const int calls=shader_calls;
        check(controller.render(render,effect.ptr()),"hidden native render must complete");
        check(displayed_frame==frame && compositions==frame+1,
              "hiding effects must keep captured gameplay and HUD advancing every frame");
        check(shader_calls==calls,"hide mode must bypass shader application");
        check(input.references==0,"native texture retain/release must stay balanced");
        check(!output_surface.shown && processor.data[84]==1 && raw_presentations==frame,
              "animated composition surfaces must hide and present an untransformed live frame instead");
    }
    const auto original_begin=*reinterpret_cast<const Ptr*>(effect.ptr()+176);
    const auto original_end=*reinterpret_cast<const Ptr*>(effect.ptr()+184);
    const auto original_capacity=*reinterpret_cast<const Ptr*>(effect.ptr()+192);
    check(render_fault_restores(controller,effect.ptr()),"SEH render failure fixture must reach its handler");
    check(*reinterpret_cast<const Ptr*>(effect.ptr()+176)==original_begin &&
          *reinterpret_cast<const Ptr*>(effect.ptr()+184)==original_end &&
          *reinterpret_cast<const Ptr*>(effect.ptr()+192)==original_capacity,
          "SEH must restore native shader ownership before the next frame");
    bool caught=false;
    try { controller.render(throwing_render,effect.ptr()); }
    catch(const std::runtime_error&) { caught=true; }
    check(caught && *reinterpret_cast<const Ptr*>(effect.ptr()+176)==original_begin &&
          shaders[0]==reinterpret_cast<Ptr>(&shader) && shaders[2]==reinterpret_cast<Ptr>(&shader),
          "C++ exceptions must restore native shader entries without changing their references");
    controller.restore();
    input.frame=121;
    check(controller.render(render,effect.ptr()) && displayed_frame==121 && shader_calls==4,
          "clearing hide mode must resume native shaders without rebuilding the world");
    composition_surface=nullptr;
    std::cout<<"120 native capture/composition frames, shader bypass and exception restoration passed\n";
}
void native_live2d_regression(HMODULE module) {
    const auto discovered=training::autoplay_discovery::resolve(module);
    check(discovered && discovered.layout.song_visuals,"native song presentation discovery");
    auto layout=discovered.layout.song_visuals;
    layout.visible=visible; layout.set_visible=set_visible;
    const auto image=reinterpret_cast<Ptr>(module);
    // Frozen reference fixture only; verify RTTI before using this evidence RVA.
    // GameExBgView's +624 actors contain CLive2DCrewComponent, NOT CLayer.
    const auto component_table=image+0x8ef440;
    const auto col=*reinterpret_cast<const Ptr*>(component_table-8);
    const auto type=image+*reinterpret_cast<const unsigned*>(col+12);
    check(std::strcmp(reinterpret_cast<const char*>(type+16),".?AVCLive2DCrewComponent@@")==0,
          "reference Live2D fixture RTTI changed");
    Bytes<512> world;
    Bytes<736> background;
    Bytes<40> actor;
    Bytes<64> component;
    Bytes<128> model_entry;
    Bytes<16> instance;
    Bytes<16> hidden_instance;
    Bytes<704> model;
    Bytes<704> hidden_model;
    background.put(0,layout.ex_bg);
    actor.put(16,component.ptr());
    component.put(0,component_table); component.put<unsigned char>(44,1);
    model_entry.put(8,instance.ptr()); instance.put(8,model.ptr());
    model_entry.put(72,hidden_instance.ptr()); hidden_instance.put(8,hidden_model.ptr());
    model.put<unsigned char>(573,1);
    Ptr actors[]{actor.ptr(),0},views[]{background.ptr(),0};
    background.put(624,reinterpret_cast<Ptr>(actors)); background.put(632,reinterpret_cast<Ptr>(actors)+sizeof(actors));
    world.put(440,reinterpret_cast<Ptr>(views)); world.put(448,reinterpret_cast<Ptr>(views)+sizeof(views));
    Controller controller; controller.configure(layout);
    check(controller.apply(world.ptr(),true),"native Live2D actor must not mark song visual control unavailable");
    check(component.data[44]==0,"empty native Live2D components must hide before model loading");
    controller.restore();
    component.put(16,model_entry.ptr()); component.put(24,model_entry.ptr()+128);
    check(controller.apply(world.ptr(),true),"loaded native Live2D actor must remain supported");
    check(component.data[44]==0 && model.data[573]==0,"native Live2D visibility must hide both component and model");
    controller.restore();
    check(component.data[44]==1 && model.data[573]==1,"native Live2D visibility must restore after the switch is cleared");
    check(hidden_model.data[573]==0,"restoration must preserve models that were originally hidden");
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        sprite_effect_regression();
        if(argc==2) {
            const auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
            check(module!=nullptr,"native module mapping failed");
            try { native_render_regression(module); native_live2d_regression(module); }
            catch(...) { FreeLibrary(module); throw; }
            FreeLibrary(module);
        }
        Controller control;
        Layout layout{1,2,3,4,5,6,7,8,visible,set_visible};
        layout.live2d=9; layout.set_live2d_visible=set_live2d_visible;
        layout.present=present_stub;
        control.configure(layout);
        Bytes<512> world;
        Bytes<736> background;
        Bytes<448> bg_entry;
        Bytes<32> actor;
        Bytes<112> autopara;
        Bytes<64> head,node;
        Bytes<96> post;
        Bytes<248> processor;
        Bytes<80> post_entry;
        Bytes<224> effect;
        Layer bg_layer,auto_layer,pooled_layer,post_layer,lane_layer;
        Bytes<64> live_component;
        live_component.put(0,layout.live2d); live_component.put<unsigned char>(44,1);
        post_layer.shown=false;
        background.put(0,layout.ex_bg);
        background.put(536,bg_entry.ptr()); background.put(544,bg_entry.ptr()+448);
        bg_entry.put(304,reinterpret_cast<Ptr>(&bg_layer));
        actor.put(16,live_component.ptr());
        Ptr actors[]{actor.ptr(),0};
        background.put(624,reinterpret_cast<Ptr>(actors)); background.put(632,reinterpret_cast<Ptr>(actors)+sizeof(actors));
        autopara.put(0,layout.autopara); autopara.put(48,head.ptr());
        head.put(8,node.ptr()); head.put<unsigned char>(25,1);
        node.put(0,head.ptr()); node.put(16,head.ptr()); node.put(40,reinterpret_cast<Ptr>(&auto_layer));
        Ptr pool_block[]{reinterpret_cast<Ptr>(&pooled_layer),0};
        Ptr pool_map[]{reinterpret_cast<Ptr>(pool_block)};
        autopara.put(72,reinterpret_cast<Ptr>(pool_map)); autopara.put<Ptr>(80,1); autopara.put<Ptr>(96,1);
        post.put(0,layout.post); post.put(64,processor.ptr());
        // The view holds shared_ptr<Processor>, rather than the processor inline.
        Ptr processors[]{processor.ptr(),0};
        post.put(64,reinterpret_cast<Ptr>(processors)); post.put(72,reinterpret_cast<Ptr>(processors)+sizeof(processors));
        processor.put(40,post_entry.ptr()); processor.put(48,post_entry.ptr()+80);
        processor.put<unsigned char>(84,1);
        post_entry.put(0,reinterpret_cast<Ptr>(&post_layer));
        effect.put(0,layout.priority); processor.put(232,effect.ptr());
        effect.put(16,layout.target); effect.put<unsigned char>(24,1);
        Ptr views[]{background.ptr(),0,autopara.ptr(),0,post.ptr(),0};
        world.put(440,reinterpret_cast<Ptr>(views)); world.put(448,reinterpret_cast<Ptr>(views)+sizeof(views));
        check(control.apply(world.ptr(),false) && bg_layer.shown,"disabled control preserves presentation");
        check(control.apply(world.ptr(),true) && auto_layer.shown && pooled_layer.shown,
              "hiding song effects must preserve ordinary autopara decorations and pooled layers");
        check(control.apply(world.ptr(),true),"validated presentation must be hidden");
        check(!bg_layer.shown && auto_layer.shown && pooled_layer.shown && !live_component.data[44] && !post_layer.shown,
              "extra overlays and Live2D hide while ordinary background decorations remain unchanged");
        check(lane_layer.shown && control.hides_view(background.ptr()) && !control.hides_view(world.ptr()),
              "normal lane and unowned views remain visible");
        check(control.hides_effect(effect.ptr()) && !control.hides_effect(1),"only owned song shaders bypass rendering");
        check(effect.data[24]==1,"capture must continue refreshing the gameplay frame");
        check(processor.data[84]==1,"composition callbacks must continue presenting the gameplay frame");
        control.restore();
        check(effect.data[24]==1,"restoring presentation also restores the capture target");
        check(processor.data[84]==1,"restoring presentation restores direct-priority callbacks");
        check(bg_layer.shown && live_component.data[44] && auto_layer.shown && pooled_layer.shown && !post_layer.shown,
              "restore preserves both originally visible and originally hidden layers");
        check(!control.hides_effect(effect.ptr()) && !control.hides_view(background.ptr()),"restore removes ownership before native updates");
        // Autopara scenery is neither inspected nor modified.
        autopara.put<Ptr>(48,1); autopara.put<Ptr>(96,10000000);
        check(control.apply(world.ptr(),true) && !bg_layer.shown && auto_layer.shown && pooled_layer.shown,
              "ordinary background containers cannot disable or change effect suppression");
        control.restore();
        bg_layer.shown=false; post_layer.shown=true;
        check(control.apply(world.ptr(),true) && control.apply(world.ptr(),false),"toggle off restores native presentation immediately");
        check(!bg_layer.shown && post_layer.shown,"native visibility changes between frames become the new baseline");
        bg_layer.shown=true;
        // Reject a later malformed view transactionally after hiding earlier roots.
        post.put(72,reinterpret_cast<Ptr>(processors)+3);
        check(!control.apply(world.ptr(),true) && bg_layer.shown && live_component.data[44] && auto_layer.shown,
              "invalid context rolls back every already-hidden layer");
        check(!control.hides_effect(effect.ptr()),"invalid context cannot suppress unrelated rendering");
        check(control.error().find("post-effect render pipeline")!=std::string::npos,
              "context failure diagnostics must name the rejected object family");
        post.put(72,reinterpret_cast<Ptr>(processors)+sizeof(processors));
        check(control.apply(world.ptr(),true),"valid next frame can be recollected");
        control.configure({});
        check(bg_layer.shown && live_component.data[44] && !control.apply(world.ptr(),true),"reset restores owners and rejects an unverified layout");
        control.configure(layout);
        check(!control.apply(1,true),"unreadable native context must fail without crashing");
        // Movie layers use a derived vtable but retain CLayer at offset zero.
        Bytes<512> metadata;
        metadata.put(0x18,metadata.ptr()+0x40); metadata.put(0x28,metadata.ptr()+0x60);
        for(unsigned col:{0x40u,0x60u}) {
            metadata.put<unsigned>(col,1); metadata.put<unsigned>(col+12,0x140);
            metadata.put<unsigned>(col+16,0x80); metadata.put<unsigned>(col+20,col);
        }
        metadata.put<unsigned>(0x88,1); metadata.put<unsigned>(0x8c,0xa0);
        metadata.put<unsigned>(0xa0,0x100); metadata.put<unsigned>(0x100,0x140);
        metadata.put<int>(0x10c,-1);
        Layer movie_layer{metadata.ptr()+0x30,2,true};
        auto derived_layout=layout; derived_layout.layer=metadata.ptr()+0x20;
        control.configure(derived_layout);
        post_entry.put(0,reinterpret_cast<Ptr>(&movie_layer));
        world.put(440,reinterpret_cast<Ptr>(views)+32);
        world.put(448,reinterpret_cast<Ptr>(views)+sizeof(views));
        // Native loaders construct output roots as CLayer, not arbitrary derived objects.
        check(!control.apply(world.ptr(),true) && movie_layer.shown,"unverified output interfaces must fail without mutating visibility");
        control.restore(); check(movie_layer.shown,"native output visibility must remain unchanged");
        metadata.put<int>(0x108,4);
        check(!control.apply(world.ptr(),true) && movie_layer.shown,"unverified output surfaces must not be reinterpreted");
        std::cout<<"Song presentation ownership, visibility, live layers, restoration and rollback passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
