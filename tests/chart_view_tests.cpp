#include "ui/chart_view.hpp"
#include "ui/preferences.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
void chart_preferences(int measures,bool follow) {
    auto options=training::preferences().snapshot();
    options.chart_column_measures=measures; options.follow_playback=follow;
    std::string error;
    check(training::preferences().save(options,error),"chart options must save");
}
ImGuiWindow* timeline() {
    for (auto* window:ImGui::GetCurrentContext()->Windows)
        if (std::string_view(window->Name).find("chart-timeline")!=std::string_view::npos) return window;
    throw std::runtime_error("timeline child missing");
}
float test_height=740;
void frame(const training::State& state,training::Session* commands=nullptr) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({900,test_height});
    ImGui::Begin("Chart test",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize);
    training::draw_chart_preview(state,commands);
    ImGui::End(); ImGui::Render();
}
float marker_y() {
    float sum{}; unsigned count{};
    for (const auto& vertex:timeline()->DrawList->VtxBuffer) if (vertex.col==IM_COL32(255,223,115,255)) {
        sum+=vertex.pos.y; ++count;
    }
    check(count>0,"visible playback marker missing"); return sum/count;
}
void click(ImVec2 pos,const training::State& state) {
    auto& io=ImGui::GetIO();
    io.AddMousePosEvent(pos.x,pos.y); frame(state);
    io.AddMouseButtonEvent(0,true); frame(state);
    io.AddMouseButtonEvent(0,false); frame(state); frame(state);
}
struct Game final:training::Backend {
    bool paused{}; unsigned seeks{}; float position{};
    bool pause(bool value) override { paused=value; return true; }
    bool seek(float value) override { position=value; ++seeks; return true; }
    bool rate(float) override { return true; }
    bool finish() override { return true; }
};
ImVec2 center_for_color(ImU32 color) {
    ImVec2 low{10000,10000},high{-10000,-10000}; unsigned count{};
    for (const auto& vertex:timeline()->DrawList->VtxBuffer) if (vertex.col==color) {
        low.x=std::min(low.x,vertex.pos.x); low.y=std::min(low.y,vertex.pos.y);
        high.x=std::max(high.x,vertex.pos.x); high.y=std::max(high.y,vertex.pos.y); ++count;
    }
    check(count>0,"selection handle or body missing");
    return {(low.x+high.x)*.5f,(low.y+high.y)*.5f};
}
void selection_scenarios(const std::shared_ptr<const training::Chart>& chart) {
    training::Session session; Game game;
    session.begin({"test.vox",1,0,150,4,1,false,chart},true);
    const auto generation=session.snapshot().generation;
    session.submit({generation,training::Action::Enable});
    session.submit({generation,training::Action::SetRegion,2,6});
    session.submit({generation,training::Action::SetLoop,1});
    session.tick(0,false,game);
    session.submit({generation,training::Action::Resume}); session.tick(0,true,game);
    auto live=[&] { frame(session.snapshot(),&session); session.tick(0,false,game); };
    auto& io=ImGui::GetIO();
    auto press=[&](ImVec2 pos) { io.AddMousePosEvent(pos.x,pos.y); live(); io.AddMouseButtonEvent(0,true); live(); };
    auto drag=[&](ImVec2 pos) { io.AddMousePosEvent(pos.x,pos.y); live(); };
    auto release=[&] { io.AddMouseButtonEvent(0,false); live(); live(); };
    live(); live(); live();
    auto in_tab=center_for_color(IM_COL32(34,164,145,255));
    press(in_tab); drag({in_tab.x,in_tab.y-30});
    check(io.MouseDown[0] && session.snapshot().in>2.5f && session.snapshot().out==6,
          "dragging IN must resize in real time before release");
    release();
    auto out_tab=center_for_color(IM_COL32(181,122,48,255));
    const float fixed_in=session.snapshot().in;
    press(out_tab); drag({out_tab.x,out_tab.y-30});
    check(io.MouseDown[0] && session.snapshot().out>6.5f && session.snapshot().in==fixed_in,
          "dragging OUT must resize without moving IN");
    release();
    const auto before=session.snapshot();
    auto body=center_for_color(IM_COL32(55,194,160,44));
    press(body); drag({body.x+168,body.y});
    auto after=session.snapshot();
    check(after.in>before.out && std::abs((after.out-after.in)-(before.out-before.in))<.001f,
          "body drag must cross columns atomically while preserving length");
    release();
    body=center_for_color(IM_COL32(55,194,160,44));
    press(body); drag({body.x-168,timeline()->InnerRect.Max.y-1}); release();
    check(std::abs(session.snapshot().in)<.001f,"whole selection must clamp to track start");
    session.submit({generation,training::Action::SetLoop,0}); live(); live();
    check(!session.snapshot().loop,"disabled loop must dim its chart selection");
    const auto inactive=session.snapshot();
    in_tab=center_for_color(IM_COL32(34,164,145,63));
    center_for_color(IM_COL32(55,194,160,11));
    press(in_tab); drag({in_tab.x,in_tab.y-45}); release();
    check(session.snapshot().in==inactive.in && session.snapshot().out==inactive.out,
          "loop-off selection must remain visible, dimmed and noninteractive");
    const auto seeks=game.seeks;
    session.tick(inactive.out+1,false,game);
    check(game.seeks==seeks && !game.paused,"inactive selection must not seek or pause at OUT");
    session.submit({generation,training::Action::SetLoop,1}); live(); live();
    check(session.snapshot().loop,"enabling loop must reactivate the existing chart selection");
    out_tab=center_for_color(IM_COL32(181,122,48,255));
    press(out_tab); drag({out_tab.x,out_tab.y+1000}); release();
    check(session.snapshot().out>=session.snapshot().in+.05f && session.snapshot().out<.06f,
          "OUT drag must clamp to the minimum region length instead of crossing IN");
    auto forward=session.snapshot(); forward.position=120; frame(forward); frame(forward);
    marker_y();
    check(training::preferences().snapshot().follow_playback,"selection editing must not disable following");
}
void musical_pointer_scenarios() {
    auto chart=std::make_shared<training::Chart>();
    chart->measures={{0,0},{1.125f,1},{1.875f,2}};
    const float times[]{0,.375f,.625f,.875f,1.125f,1.375f,1.625f,1.875f,2.375f,2.875f,3.375f};
    for(unsigned i=0;i<std::size(times);++i) chart->beats.push_back({times[i],i});
    chart->tempos={{0,120},{.25f,240},{1.875f,120}};
    chart->timing=training::MusicalTimeline::build(chart->measures,chart->beats,chart->tempos);
    training::Session session; Game game;
    for(int i=0;i<10;++i) session.begin({"variable.vox",1,0,3.875f,0,0,false,chart},true);
    const auto generation=session.snapshot().generation;
    session.submit({generation,training::Action::Enable});
    chart_preferences(3,false);
    auto live=[&] { frame(session.snapshot(),&session); session.tick(game.position,game.paused,game); };
    live(); live(); live();
    auto& io=ImGui::GetIO();
    const auto origin=timeline()->DC.CursorStartPos;
    const float height=timeline()->InnerRect.GetHeight()-timeline()->WindowPadding.y*2-2;
    auto point=[&](float measure) { return ImVec2{origin.x+80,origin.y+height-20-measure/3*(height-44)}; };
    auto double_click=[&](float measure,int button) {
        for(int i=0;i<25;++i) live();
        const auto p=point(measure); io.AddMousePosEvent(p.x,p.y); live();
        io.AddMouseButtonEvent(button,true); live(); io.AddMouseButtonEvent(button,false); live();
        io.AddMouseButtonEvent(button,true); live(); io.AddMouseButtonEvent(button,false); live(); live();
    };
    double_click(.5f,0); double_click(1.5f,1);
    check(std::abs(session.snapshot().in-.625f)<.015f && std::abs(session.snapshot().out-1.5f)<.015f,
          "double clicks at half-measure positions must invert actual BPM and 3/4 timing");
    session.submit({generation,training::Action::SetLoop,1}); live(); live();
    auto body=center_for_color(IM_COL32(55,194,160,44));
    io.AddMousePosEvent(body.x,body.y); live(); io.AddMouseButtonEvent(0,true); live();
    io.AddMousePosEvent(body.x,body.y-(height-44)/3); live();
    io.AddMouseButtonEvent(0,false); live(); live();
    check(std::abs(session.snapshot().in-1.5f)<.03f && std::abs(session.snapshot().out-2.875f)<.03f,
          "moving the whole selection one measure must preserve its musical span rather than its seconds");
}
void mouse_mark_scenarios(const std::shared_ptr<const training::Chart>& chart) {
    training::Session session; Game game;
    // Use a fresh view generation after the preceding selection fixture.
    session.begin({"test.vox",1,0,150,4,1,false,chart},true);
    session.begin({"test.vox",1,0,150,4,1,false,chart},true);
    const auto generation=session.snapshot().generation;
    session.submit({generation,training::Action::Enable});
    session.submit({generation,training::Action::SetRegion,1,11});
    session.tick(0,false,game);
    session.submit({generation,training::Action::Resume}); session.tick(0,true,game);
    auto live=[&] { frame(session.snapshot(),&session); session.tick(0,false,game); };
    auto& io=ImGui::GetIO();
    auto settle=[&] { for (int i=0;i<25;++i) live(); };
    auto point=[&](float time,float seconds=12.f) {
        const auto* window=timeline();
        const auto origin=window->DC.CursorStartPos;
        const float height=window->InnerRect.GetHeight()-window->WindowPadding.y*2-2;
        const float column=std::floor(time/seconds),fraction=time/seconds-column;
        return ImVec2{origin.x+column*168+36+44,origin.y+height-20-fraction*(height-44)};
    };
    auto double_press=[&](ImVec2 pos,int button) {
        settle(); io.AddMousePosEvent(pos.x,pos.y); live();
        io.AddMouseButtonEvent(button,true); live();
        io.AddMouseButtonEvent(button,false); live();
        io.AddMouseButtonEvent(button,true); live();
        check(io.MouseDoubleClicked[button],"mouse fixture must generate an actual double click");
    };
    auto release=[&](int button) { io.AddMouseButtonEvent(button,false); live(); live(); };
    live(); live(); live();
    double_press(point(3.5f),0);
    // ImGui rounds mouse coordinates to physical pixels; allow one plot pixel.
    check(std::abs(session.snapshot().in-3.5f)<.04f && session.snapshot().position==0 && !session.snapshot().loop,
          "left double click must mark pointer time rather than playback and keep Loop off");
    release(0);
    double_press(point(7.25f),1); release(1);
    check(std::abs(session.snapshot().out-7.25f)<.04f,"right double click must mark OUT at pointer time");
    session.submit({generation,training::Action::SetLoop,1}); session.tick(0,false,game); live();
    auto pos=point(5);
    double_press(pos,0);
    const auto marked=session.snapshot();
    io.AddMousePosEvent(pos.x,pos.y-30); live();
    check(std::abs(marked.in-5)<.04f && session.snapshot().in==marked.in && session.snapshot().out==marked.out,
          "double click inside the selection must not become a held body drag");
    release(0);
    auto gutter=point(6); gutter.x=timeline()->DC.CursorStartPos.x+10;
    double_press(gutter,1); release(1);
    check(session.snapshot().out==marked.out,"gutter double clicks must not create marks");
    const auto bar=ImGui::GetWindowScrollbarRect(timeline(),ImGuiAxis_X);
    for (int i=0;i<3;++i) {
        io.AddMousePosEvent(bar.Max.x-4,(bar.Min.y+bar.Max.y)*.5f); live();
        io.AddMouseButtonEvent(0,true); live(); io.AddMouseButtonEvent(0,false); live(); live();
    }
    check(timeline()->Scroll.x>1000,"marking fixture must browse to later columns");
    double_press(point(153),1); release(1);
    check(session.snapshot().out==marked.out,"unused time beyond song end in the last column must be ignored");
    double_press(point(145),1); release(1);
    check(std::abs(session.snapshot().out-145)<.04f,"pointer time must account for horizontal scrolling");
    const auto* parent=ImGui::FindWindowByName("Chart test"); const auto& style=ImGui::GetStyle();
    const float fit_width=ImGui::CalcTextSize("Fit").x+2*style.FramePadding.x;
    const ImVec2 fit{parent->Pos.x+parent->Size.x-style.WindowPadding.x-fit_width*.5f,
        timeline()->Pos.y-style.ItemSpacing.y-ImGui::GetFrameHeight()*.5f};
    io.AddMousePosEvent(fit.x,fit.y); live(); io.AddMouseButtonEvent(0,true); live();
    // Zoom changes rebuild the chart next frame, then ImGui updates its scroll extent.
    io.AddMouseButtonEvent(0,false); live(); live(); live();
    check(timeline()->ScrollMax.x<2,"marking fixture must fit song to five columns");
    double_press(point(35,30),0); release(0);
    check(std::abs(session.snapshot().in-35)<.1f,"marking must use the current column time scale after zoom");
    session.submit({generation,training::Action::Disable}); session.tick(0,false,game); live();
    double_press(point(45,30),0); release(0);
    check(std::abs(session.snapshot().in-35)<.1f,"training off must ignore double click marking");
}
}
int main() {
    ImGui::CreateContext();
    const auto config=std::filesystem::temp_directory_path()/(L"training-chart-view-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    try {
        std::string config_error;
        check(training::preferences().load(config,config_error),"chart preference fixture must initialize");
        auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1000,850}; io.DeltaTime=1.f/60;
        unsigned char* pixels{}; int width{},height{}; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        auto chart=std::make_shared<training::Chart>();
        chart->notes={{3,3,0},{1,60,4},{32,32,5},{149,149,3}};
        chart->lasers.push_back({0,1,{{2,0},{32,.5f},{32,1},{100,0}}});
        // A 4/4 measure is exactly one second at 240 BPM. Existing pointer
        // fixtures therefore retain convenient numeric coordinates in bars.
        chart->tempos={{0,240}};
        for(unsigned m=0;m<150;++m) chart->measures.push_back({static_cast<float>(m),m});
        for(unsigned b=0;b<600;++b) chart->beats.push_back({b*.25f,b});
        chart->timing=training::MusicalTimeline::build(chart->measures,chart->beats,chart->tempos);
        training::State state; state.generation=1; state.playing=true; state.track.duration=150;
        state.track.preview=chart; state.position=34;
        frame(state); frame(state); frame(state);
        auto* window=timeline();
        check(window->Scroll.x>0 && window->ScrollMax.x>5400,"default 4-measure columns must produce 38 browsable columns");
        check(window->ScrollMax.y<1,"vertical chart columns must fit without a vertical scrollbar");
        const auto y1=marker_y();
        state.position+=.5f; frame(state);
        check(marker_y()<y1-10,"playhead must advance upward with song time");
        const auto paused=marker_y(); frame(state);
        check(std::abs(marker_y()-paused)<.1f,"paused song time must keep marker stationary");
        state.position=36; frame(state); frame(state);
        check(marker_y()>paused+150,"crossing a column boundary must restart the marker at the next column bottom");
        float marker_left=10000,marker_right=-10000;
        for (const auto& vertex:window->DrawList->VtxBuffer) if (vertex.col==IM_COL32(255,223,115,255)) {
            marker_left=std::min(marker_left,vertex.pos.x); marker_right=std::max(marker_right,vertex.pos.x);
        }
        check(marker_right-marker_left<150,"a boundary time must have exactly one column playhead");
        state.position=4; frame(state); frame(state);
        check(window->Scroll.x<2*168,"seek or loop rewind must bring earlier playback into view");
        const auto bar=ImGui::GetWindowScrollbarRect(window,ImGuiAxis_X);
        click({bar.Max.x-12,(bar.Min.y+bar.Max.y)*.5f},state);
        state.position=120; frame(state); frame(state);
        marker_y();
        check(training::preferences().snapshot().follow_playback,"manual browsing must not disable following");
        chart_preferences(3,false); frame(state);
        click({bar.Min.x+12,(bar.Min.y+bar.Max.y)*.5f},state);
        const auto browsed=window->Scroll.x;
        state.position=60; frame(state); frame(state);
        check(std::abs(window->Scroll.x-browsed)<1,"explicitly disabled follow must permit manual browsing");
        chart_preferences(3,true); frame(state); frame(state);
        state.loop=true; state.in=50; state.out=70; state.paused=true;
        click({bar.Max.x-4,(bar.Min.y+bar.Max.y)*.5f},state);
        const float paused_scroll=window->Scroll.x;
        check(paused_scroll>1000,"paused playback must allow browsing outside the loop selection");
        state.position=5; frame(state); frame(state);
        check(std::abs(window->Scroll.x-paused_scroll)<1,
              "position changes must not override manual browsing while paused");
        check(training::preferences().snapshot().follow_playback,
              "paused browsing must not change the follow preference");
        state.paused=false; frame(state); frame(state);
        check(window->Scroll.x<4*168,"resuming playback must follow the playhead again");
        state.loop=false;
        const auto* parent=ImGui::FindWindowByName("Chart test");
        const auto& style=ImGui::GetStyle();
        marker_y();
        state.generation=2; state.position=0; frame(state); frame(state);
        check(window->Scroll.x<1,"new track must reset preview range");
        const float fit_width=ImGui::CalcTextSize("Fit").x+2*style.FramePadding.x;
        const ImVec2 fit{parent->Pos.x+parent->Size.x-style.WindowPadding.x-fit_width*.5f,
            timeline()->Pos.y-style.ItemSpacing.y-ImGui::GetFrameHeight()*.5f};
        click(fit,state); frame(state);
        check(window->ScrollMax.x<2,"Fit must expose the complete song range");
        state.generation=3; state.position=32; frame(state); frame(state);
        check(window->ScrollMax.x>7000,"new song must restore the configured duration after Fit");
        chart_preferences(8,true); frame(state); frame(state);
        check(window->ScrollMax.x>2200 && window->ScrollMax.x<2400,"Option duration changes must apply to the current chart");
        state.generation=4; frame(state); frame(state);
        check(window->ScrollMax.x>2200 && window->ScrollMax.x<2400,"configured duration must survive song changes");
        chart_preferences(12,true);
        selection_scenarios(chart);
        chart_preferences(12,false);
        mouse_mark_scenarios(chart);
        musical_pointer_scenarios();
        test_height=340; frame(state); frame(state);
        const auto* compact=ImGui::FindWindowByName("Chart test");
        check(timeline()->ScrollMax.y<1 && compact->ScrollMax.y<1,
              "compact chart must fit vertically while keeping its zoom controls visible");
        std::filesystem::remove(config); ImGui::DestroyContext();
        std::cout<<"Chart preview interaction scenarios passed\n"; return 0;
    } catch (const std::exception& e) {
        std::error_code cleanup_error; std::filesystem::remove(config,cleanup_error);
        ImGui::DestroyContext(); std::cerr<<e.what()<<'\n'; return 1;
    }
}
