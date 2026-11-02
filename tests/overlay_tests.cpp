#include "ui/overlay.hpp"
#include "ui/preferences.hpp"
#include "core/judgment.hpp"
#include "training_version.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <algorithm>

namespace {
struct TransportBackend final:training::Backend {
    bool paused{},reversing{},finished{}; float cursor{};
    bool pause(bool value) override { paused=value; return true; }
    bool seek(float value) override { cursor=value; return true; }
    bool rate(float) override { return true; }
    bool finish() override { finished=true; return true; }
    bool reverse(bool value) override { reversing=value; return true; }
    training::GaugeConfig gauge_config;
    bool gauge(const training::GaugeConfig& config) override { gauge_config=config; return true; }
    bool fixed_speed{};
    bool lane_speed(bool value) override { fixed_speed=value; return true; }
    void release_lane_speed() override { fixed_speed=false; }
};
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void only_message(const std::string& text,const char* expected) {
    std::istringstream stream(text); std::string line; unsigned count{};
    while (std::getline(stream,line)) {
        const auto start=line.find_first_not_of(" \t\r");
        if (start==std::string::npos) continue;
        line=line.substr(start,line.find_last_not_of(" \t\r")-start+1);
        auto menu=line; menu.erase(std::remove_if(menu.begin(),menu.end(),[](char c){return c==' ' || c=='[' || c==']';}),menu.end());
        if (line=="Training Master" || menu=="About" || menu=="Option" || menu=="OptionAbout") continue;
        check(line==expected,("idle/error panel unexpected text: "+line).c_str()); ++count;
    }
    check(count==1,"specified message must be rendered exactly once");
}
bool has_color(ImU32 color) {
    const auto* data=ImGui::GetDrawData();
    for (const auto* list:data->CmdLists) for (const auto& vertex:list->VtxBuffer) if (vertex.col==color) return true;
    return false;
}
std::string render(training::Session& session) {
    ImGui::GetCurrentContext()->LogBuffer.clear();
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    training::draw_panel(session);
    const std::string text=ImGui::GetCurrentContext()->LogBuffer.c_str();
    ImGui::LogFinish();
    ImGui::Render();
    return text;
}
std::string render_counter(training::Session& session) {
    ImGui::GetCurrentContext()->LogBuffer.clear();
    ImGui::NewFrame(); ImGui::LogToBuffer();
    training::draw_judgment_counter(session.snapshot());
    const std::string text=ImGui::GetCurrentContext()->LogBuffer.c_str();
    ImGui::LogFinish(); ImGui::Render(); return text;
}
bool counter_color(ImU32 color,float left,float right) {
    const auto* window=ImGui::FindWindowByName("##judgment-counter");
    if(!window) return false;
    const float min=window->Pos.x+window->Size.x*left,max=window->Pos.x+window->Size.x*right;
    for(const auto& vertex:window->DrawList->VtxBuffer)
        if(vertex.col==color && vertex.pos.x>=min && vertex.pos.x<=max) return true;
    return false;
}
void activate_transport(training::Session& session,const char* id) {
    ImGuiWindow* controls{};
    const bool finish=std::string_view(id)=="Finish Immediately";
    for (auto* window:ImGui::GetCurrentContext()->Windows) {
        const std::string_view name(window->Name);
        if(finish ? (name.find("training-sidebar")!=name.npos && name.find("training-controls")==name.npos)
                  : name.find("training-controls")!=name.npos) controls=window;
    }
    check(controls!=nullptr,"transport control window must exist");
    const auto item=controls->GetID(training::i18n::label(id));
    ImGui::ActivateItemByID(item); render(session);
}

}
int main() {
    ImGui::CreateContext();
    const auto config=std::filesystem::temp_directory_path()/(L"training-overlay-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    try {
        std::string config_error;
        check(training::preferences().load(config,config_error),"overlay preference fixture must initialize");
        auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1360,900}; io.DeltaTime=1.f/60;
        unsigned char* pixels{}; int width{},height{};
        io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        training::Session session;
        training::show_overlay(true);
        training::set_overlay_status("Game hooks ready");
        only_message(render(session),"Choose a track to start training.");
        check(has_color(IM_COL32(255,255,0,255)),"waiting message must be yellow");
        training::set_overlay_status("play_state: signature not found",true);
        const auto failure=render(session);
        only_message(failure,"Hook entry not found!");
        check(has_color(IM_COL32(255,51,51,255)),"hook failure message must be red");
        training::set_overlay_status("Game hooks ready");
        const auto ready=render(session);
        only_message(ready,"Choose a track to start training.");
        training::Track track{"test.vox",12,3,120.f}; track.autoplay_ready=track.camera_ready=true;
        track.lane_speed_ready=true; track.gauge_ready=true;
        auto info=std::make_shared<training::SongInfo>();
        info->title="Test song & title"; info->artist="Test artist"; info->difficulty="GRAVITY  Lv. 18.7";
        track.info=info;
        auto pcm=std::make_shared<training::PcmClip>(); pcm->samples.resize(2,0.f);
        track.reverse_audio=std::make_shared<training::ReverseAsset>(pcm);
        session.begin(track,true);
        const auto playing=render(session);
        check(playing.find("Training is off")==std::string::npos,"training-off explanation must not be displayed");
        training::judgment::set_available(true);
        training::judgment::reset();
        training::judgment::record(2,-.020f,true);
        auto counter=render_counter(session);
        check(counter.find("S-CRITICAL")!=std::string::npos && counter.find("-20.0")!=std::string::npos && counter.find("20.0 ms")!=std::string::npos,
              "judgment window must display categories, signed mean and absolute mean");
        training::judgment::reset();
        for(int i=0;i<2;++i) training::judgment::record(3,-.010f,true);
        for(int i=0;i<3;++i) training::judgment::record(4,0.f,true);
        for(int i=0;i<4;++i) training::judgment::record(7,.010f,true);
        training::judgment::set_s_critical_enabled(false);
        counter=render_counter(session);
        check(counter.find("S-CRITICAL")==std::string::npos && counter.find("CRITICAL")!=std::string::npos,
              "disabled S-CRITICAL must display the five-column layout");
        const auto* standard_table=ImGui::FindWindowByName("##judgment-counter");
        check(standard_table && counter.find("9")!=std::string::npos,
              "standard CRITICAL must combine early, central and late counts");
        check(training::judgment::snapshot().counts[2]==2 && training::judgment::snapshot().counts[3]==3 &&
              training::judgment::snapshot().counts[4]==4,"switching layouts must preserve the original counts");
        training::judgment::set_s_critical_enabled(true);
        counter=render_counter(session);
        check(counter.find("S-CRITICAL")!=std::string::npos,"enabled S-CRITICAL must restore seven columns");
        training::judgment::reset(); training::judgment::record(2,-.020f,true);
        const auto red=ImGui::ColorConvertFloat4ToU32({1.f,.42f,.58f,1.f});
        const auto blue=ImGui::ColorConvertFloat4ToU32({.33f,.70f,1.f,1.f});
        render_counter(session);
        const auto* initial_panel=ImGui::FindWindowByName("###Training Master");
        const auto* initial_counter=ImGui::FindWindowByName("##judgment-counter");
        check(initial_panel && initial_counter && initial_panel->Pos.y>=initial_counter->Pos.y+initial_counter->Size.y,
              "default main window must start below the judgment counter");
        check(initial_panel->Pos.y+initial_panel->Size.y<=io.DisplaySize.y,
              "default main window must fit vertically at the normal display size");
        check(counter_color(red,0,.3f) && counter_color(blue,.7f,1) && counter_color(red,.4f,.6f),
              "default early must be red, late blue and negative average red");
        training::judgment::set_colors_swapped(true); render_counter(session);
        check(counter_color(blue,0,.3f) && counter_color(red,.7f,1) && counter_color(blue,.4f,.6f),
              "swapped setting must reverse labels and negative average");
        training::judgment::reset(); training::judgment::record(6,.020f,true); render_counter(session);
        check(counter_color(red,.4f,.6f),"positive average must use swapped late color");
        training::judgment::set_colors_swapped(false); render_counter(session);
        check(counter_color(blue,.4f,.6f),"positive average must use default late color");
        training::judgment::reset(); render_counter(session);
        check(!counter_color(red,.4f,.6f) && !counter_color(blue,.4f,.6f),"zero average must use neutral color");
        training::judgment::record(2,-.020f,true);
        training::show_overlay(false); counter=render_counter(session);
        check(counter.find("S-CRITICAL")==std::string::npos,
              "judgment window must default to hidden when the main overlay is closed");
        auto counter_preferences=training::preferences().snapshot(); counter_preferences.always_show_judgment_counter=true;
        check(training::preferences().save(counter_preferences,config_error),"counter visibility fixture must save");
        counter=render_counter(session);
        check(counter.find("S-CRITICAL")!=std::string::npos,"always-show option must keep the counter visible");
        counter_preferences.always_show_judgment_counter=false;
        check(training::preferences().save(counter_preferences,config_error),"counter visibility fixture must restore default");
        training::show_overlay(true);
        check(playing.find("Setting")!=std::string::npos && playing.find("AutoPlay")!=std::string::npos &&
              playing.find("AutoPlay diagnostics")==std::string::npos && playing.find("Block camera movement")!=std::string::npos,
              "Setting must expose AutoPlay and camera controls by default");
        for (const char* expected:{"Music ID","Title","Artist","Difficulty","Test song & title","Test artist","GRAVITY  Lv. 18.7","test.vox"})
            check(playing.find(expected)!=std::string::npos,"playing panel must display song metadata and chart path");
        check(playing.find("button objects")==std::string::npos && playing.find("laser objects")==std::string::npos,
              "chart object counts must be removed");
        check(playing.find("0:00 / 2:00")!=std::string::npos,"transport must show minutes:seconds instead of total seconds");
        check(playing.find("Follow playback")==std::string::npos && playing.find("Loop")==std::string::npos &&
              playing.find("Repeat")==std::string::npos,"Transport must use one loop icon instead of Loop/Repeat checkboxes");
        check(!session.snapshot().autoplay && !session.snapshot().enabled && !session.suppress_result(),
              "displaying AutoPlay controls must not enable training, AutoPlay or taint the track");
        check(playing.find("All")!=std::string::npos,"AutoPlay dropdown shows the default All mode");
        TransportBackend backend;
        activate_transport(session,"##transport-loop"); session.tick(0,false,backend);
        check(!session.snapshot().loop && !session.snapshot().repeat && !session.snapshot().enabled,
              "loop icon must be disabled before training starts");
        check(playing.find("Fix Lane Speed")!=std::string::npos && !session.snapshot().lane_speed_locked,
              "Setting must expose the speed lock, default off");
        for(const char* id:{"AutoPlay","Fix Lane Speed","Block camera movement","Gauge###gauge-enabled"}) {
            activate_transport(session,id); session.tick(0,false,backend);
        }
        check(!session.snapshot().enabled && !session.snapshot().autoplay && !session.snapshot().lane_speed_locked &&
              !session.snapshot().camera_locked && !session.snapshot().gauge.enabled && !session.suppress_result(),
              "settings must not activate training while it is disabled");
        activate_transport(session,"Enable training for this track"); session.tick(0,false,backend);
        check(session.snapshot().enabled && session.suppress_result(),"training must be enabled before settings can be used");
        check(render(session).find("RESULTS / UPLOAD BLOCKED FOR THIS TRACK")==std::string::npos,
              "result blocking remains active without a separate explanation in the panel");
        activate_transport(session,"Fix Lane Speed"); session.tick(1,false,backend);
        check(session.snapshot().lane_speed_locked && backend.fixed_speed && session.suppress_result(),"speed checkbox enables speed lock during training");
        activate_transport(session,"Fix Lane Speed"); session.tick(1,true,backend);
        check(!session.snapshot().lane_speed_locked && !backend.fixed_speed,"speed checkbox restores native display");
        const auto gauge_label=playing.find("Gauge",playing.find("Fix Lane Speed"));
        const auto gauge_type=playing.find("EFFECTIVE");
        check(gauge_label!=std::string::npos && gauge_type!=std::string::npos && gauge_label<gauge_type,
              "gauge switch label must precede the gauge type dropdown");
        activate_transport(session,"Gauge###gauge-enabled"); session.tick(1,false,backend);
        check(session.snapshot().gauge.enabled && backend.gauge_config.enabled && session.suppress_result(),"gauge checkbox enables protected native control");
        check(render(session).find("Initial Gauge:")!=std::string::npos,"initial blood slider appears below gauge type");
        activate_transport(session,"Gauge###gauge-enabled"); session.tick(1,false,backend);
        check(!session.snapshot().gauge.enabled && !backend.gauge_config.enabled,"gauge checkbox restores native gauge");
        activate_transport(session,"Block camera movement"); session.tick(1,false,backend);
        check(session.snapshot().camera_locked && session.snapshot().enabled && session.suppress_result(),
              "camera checkbox must enable protected training");
        for(unsigned i=0;i<30 && session.snapshot().rewinding;++i) session.tick(session.snapshot().position,true,backend,.05f);
        session.submit({session.snapshot().generation,training::Action::Resume}); session.tick(0,true,backend);
        const auto rate_controls=render(session);
        for(const char* rate:{"0.50x","0.75x","1.00x","1.50x"})
            check(rate_controls.find(rate)!=std::string::npos,"requested rate shortcut must be visible");
        check(rate_controls.find("0.25x")==std::string::npos,"0.25x shortcut must be removed");
        activate_transport(session,"1.50x"); session.tick(1,false,backend);
        check(session.snapshot().rate==1.5f,"1.5x shortcut must set the playback rate");
        activate_transport(session,"Block camera movement"); session.tick(1,false,backend);
        check(!session.snapshot().camera_locked && session.suppress_result(),"camera checkbox must release view without restoring eligibility");
        activate_transport(session,"Enable training for this track"); session.tick(1,false,backend);
        check(session.snapshot().enabled && session.native_exit_locked(),"training checkbox must be locked until Finish Immediately");
        session.submit({session.snapshot().generation,training::Action::Enable}); session.tick(32,false,backend);
        render(session);
        activate_transport(session,"##transport-loop"); session.tick(32,false,backend);
        check(session.snapshot().loop && session.snapshot().repeat,"first loop click must enable continuous repeat");
        activate_transport(session,"##transport-loop"); session.tick(32,false,backend);
        check(session.snapshot().loop && !session.snapshot().repeat,"second loop click must return once and pause");
        activate_transport(session,"##transport-loop"); session.tick(32,false,backend);
        check(!session.snapshot().loop && !session.snapshot().repeat,"third loop click must disable both loop flags");
        activate_transport(session,"##transport-rewind"); session.tick(32,false,backend);
        check(session.snapshot().reversing && backend.reversing,"left icon must start continuous reverse playback");
        activate_transport(session,"##transport-pause"); session.tick(31,true,backend);
        check(session.snapshot().paused && backend.paused,"middle icon must pause reverse playback");
        activate_transport(session,"##transport-play"); session.tick(31,true,backend);
        check(!session.snapshot().reversing && !session.snapshot().paused && !backend.reversing && !backend.paused,
              "right icon must restore forward playback");
        session.tick(75,false,backend);
        check(render(session).find("1:15 / 2:00")!=std::string::npos,"minutes display must advance correctly beyond one minute");
        session.begin(track,true);
        check(render(session).find("AutoPlay")!=std::string::npos && !session.snapshot().autoplay,
              "AutoPlay controls must remain visible across tracks while AutoPlay resets off");
        auto premium=track; premium.native_quick_finish=true;
        session.begin(premium,true); backend.finished=false;
        const auto premium_panel=render(session);
        check(premium_panel.find("Finish Immediately")!=std::string::npos && premium_panel.find("Use the game's quick finish")==std::string::npos,
              "Premium Time must expose the same Finish Immediately button as Normal");
        activate_transport(session,"Finish Immediately"); session.tick(0,false,backend);
        check(!backend.finished && !session.suppress_result(),"Finish remains disabled before training has protected the track");
        session.submit({session.snapshot().generation,training::Action::Enable}); session.tick(0,false,backend);
        // The only exit must remain visible even when a long title overflows the smallest panel.
        info->title=std::string(300,'W');
        ImGui::SetWindowSize("###Training Master",{780,400}); render(session); render(session);
        ImGuiWindow *sidebar{},*content{};
        for(auto* window:ImGui::GetCurrentContext()->Windows) {
            const std::string_view name(window->Name);
            if(name.find("training-controls")!=name.npos) content=window;
            else if(name.find("training-sidebar")!=name.npos) sidebar=window;
        }
        check(sidebar && content && content->ScrollMax.y>0 && sidebar->ScrollMax.y<1,
              "long metadata must scroll independently without pushing the finish footer offscreen");
        check(sidebar->DC.CursorMaxPos.y<=sidebar->InnerRect.Max.y+1,
              "finish footer must fit inside the minimum-height panel");
        activate_transport(session,"Finish Immediately"); session.tick(0,true,backend);
        ImGui::SetWindowSize("###Training Master",{1180,680});
        check(backend.finished && !session.snapshot().enabled && session.suppress_result(),
              "Premium Time Finish must stop training through the backend while retaining no-result protection");
        const auto* panel=ImGui::FindWindowByName("###Training Master");
        check(panel!=nullptr,"main overlay window must exist");
        const auto menu=ImHashStr("##MenuBar",0,panel->ID);
        info->title="Test song & title";
        ImGui::ActivateItemByID(ImHashStr(training::i18n::label("Option"),0,menu));
        const auto option=render(session);
        check(option.find("Background opacity")!=std::string::npos && option.find("90%")!=std::string::npos,
              "Option must expose background opacity as a percentage with a 90 percent default");
        auto* options=ImGui::FindWindowByName(training::i18n::label("Option"));
        check(options!=nullptr,"Option window must open");
        check(option.find("Default measures per column")!=std::string::npos && option.find("4 measures / column")!=std::string::npos &&
              option.find("Follow playback")!=std::string::npos,"Option must expose chart defaults and follow switch");
        ImGui::ActivateItemByID(options->GetID(training::i18n::label("Follow playback"))); render(session);
        training::PreferencesStore restarted;
        check(!training::preferences().snapshot().follow_playback && restarted.load(config,config_error) && !restarted.snapshot().follow_playback,
              "Option follow switch must save immediately and survive reload");
        ImGui::ActivateItemByID(options->GetID(training::i18n::label("Follow playback"))); render(session);
        check(training::preferences().snapshot().follow_playback,"Option must also reenable following");
        check(option.find("Always show judgment counter")!=std::string::npos,"Option must expose judgment counter visibility");
        ImGui::ActivateItemByID(options->GetID(training::i18n::label("Always show judgment counter"))); render(session);
        check(training::preferences().snapshot().always_show_judgment_counter && restarted.load(config,config_error) && restarted.snapshot().always_show_judgment_counter,
              "Option counter switch must save immediately and survive reload");
        ImGui::ActivateItemByID(options->GetID(training::i18n::label("Always show judgment counter"))); render(session);
        check(!training::preferences().snapshot().always_show_judgment_counter,"counter always-show can be disabled");
        check(option.find("Language")!=std::string::npos && option.find("English")!=std::string::npos,"Option must expose an English-default language selector");
        ImGui::ActivateItemByID(options->GetID("##language")); render(session);
        ImGuiWindow* combo{};
        for(auto* window:ImGui::GetCurrentContext()->Windows)
            if(window->Active && std::string_view(window->Name).starts_with("##Combo_")) combo=window;
        check(combo!=nullptr,"language selector must open");
        ImGui::ActivateItemByID(combo->GetID("中文")); render(session);
        check(training::preferences().snapshot().language==training::i18n::Language::Chinese &&
              restarted.load(config,config_error) && restarted.snapshot().language==training::i18n::Language::Chinese,
              "selecting Chinese must save immediately and survive reload");
        const auto chinese=render(session);
        for(const char* expected:{"训练大师","选项","语言","默认每列小节数","播放控制","立即结束","曲目 ID"})
            check(chinese.find(expected)!=std::string::npos,"Chinese UI must translate menu, options, controls and metadata labels");
        check(ImGui::FindWindowByName("###Training Master")==panel,
              "Chinese title must retain the main window identity");
        check(chinese.find("每列 4 小节")!=std::string::npos,"Chinese slider units must be localized");
        check(ImGui::FindWindowByName(training::i18n::label("Option"))==options,"language switching must preserve the Option window ID");
        ImGui::ActivateItemByID(options->GetID(training::i18n::label("Close"))); render(session);
        // English IDs must remain usable for translated controls.
        session.begin(track,true); render(session);
        activate_transport(session,"Enable training for this track"); session.tick(0,true,backend);
        activate_transport(session,"Fix Lane Speed"); session.tick(0,true,backend);
        check(session.snapshot().lane_speed_locked,"translated controls must submit the same training command");
        ImGui::ActivateItemByID(ImHashStr(training::i18n::label("About"),0,menu));
        const auto chinese_about=render(session);
        check(chinese_about.find("关于训练大师")!=std::string::npos && chinese_about.find("第三方许可证")!=std::string::npos &&
              chinese_about.find("Permission is hereby granted")!=std::string::npos,"About must translate its UI and preserve the original license");
        const std::string build_date=training::about::build_date;
        check(build_date.size()==16 && build_date[4]=='-' && build_date[7]=='-' && build_date[10]==' ' && build_date[13]==':' &&
              std::all_of(build_date.begin(),build_date.end(),[](char c){return (c>='0' && c<='9') || c=='-' || c==' ' || c==':';}),
              "build date must use YYYY-MM-DD HH:MM");
        check(chinese_about.find(std::string("构建日期 ")+build_date)!=std::string::npos && chinese_about.find("Build by Takanashi Ryo")!=std::string::npos &&
              chinese_about.find("Close Beta")==std::string::npos && chinese_about.find("Copyright (c) Takanashi Ryo")==std::string::npos,
              "About must show the build date and build credit without the beta or project copyright line");
        auto* about=ImGui::FindWindowByName(training::i18n::label("About Training Master"));
        check(about!=nullptr,"translated About window must exist");
        ImGui::ActivateItemByID(about->GetID(training::i18n::label("Close"))); render(session);
        training::set_overlay_status("missing test hook",true);
        check(render(session).find("未找到 Hook 入口！")!=std::string::npos,"hook error must be localized");
        training::set_overlay_status("Game hooks ready");
        training::Session idle;
        check(render(idle).find("请选择一首曲目以开始训练。")!=std::string::npos,"idle status must be localized");
        auto english=training::preferences().snapshot(); english.language=training::i18n::Language::English;
        check(training::preferences().save(english,config_error),"language must switch back to English");
        const auto restored=render(session);
        check(restored.find("Finish Immediately")!=std::string::npos && restored.find("Training Master")!=std::string::npos &&
              ImGui::FindWindowByName("###Training Master")==panel,"English labels must restore without changing the window identity");
        ImGui::ActivateItemByID(ImHashStr(training::i18n::label("About"),0,menu));
        const auto english_about=render(session);
        check(english_about.find(std::string("Build date ")+build_date)!=std::string::npos,
              "English About must show the same build date");
        auto* english_about_window=ImGui::FindWindowByName(training::i18n::label("About Training Master"));
        check(english_about_window!=nullptr,"English About window must exist");
        ImGui::ActivateItemByID(english_about_window->GetID(training::i18n::label("Close"))); render(session);
        training::set_overlay_status("another detailed hook failure",true);
        only_message(render(session),"Hook entry not found!");
        std::filesystem::remove(config); ImGui::DestroyContext(); std::cout<<"Overlay hook-failure status scenarios passed\n"; return 0;
    } catch (const std::exception& error) {
        std::error_code cleanup_error; std::filesystem::remove(config,cleanup_error);
        ImGui::DestroyContext(); std::cerr<<error.what()<<'\n'; return 1;
    }
}
