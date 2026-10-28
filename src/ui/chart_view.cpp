#include "ui/chart_view.hpp"
#include "ui/i18n.hpp"
#include "ui/preferences.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace training {
namespace {
constexpr ImU32 blue=IM_COL32(54,139,220,195), pink=IM_COL32(193,57,151,195);
constexpr ImU32 orange=IM_COL32(226,156,57,255), white=IM_COL32(224,229,230,255);
constexpr ImU32 gold=IM_COL32(255,223,115,255);
constexpr float column_width=168.f, track_width=88.f, gutter=36.f;
enum class RegionDrag { None, In, Out, Move };
struct View {
    std::uint64_t generation{};
    int measures{4}, default_measures{4};
    bool follow{true}, reset{true}, first_frame{true};
    float in{}, out{}, anchor{}, initial_in{}, initial_out{};
    RegionDrag drag{RegionDrag::None};
    bool suppress_drag{};
};
View view;
void sync_view(const State& s) {
    const auto options=preferences().snapshot();
    if(view.generation!=s.generation) {
        view={s.generation};
        view.measures=view.default_measures=options.chart_column_measures;
        view.follow=options.follow_playback;
    }
    if(view.default_measures!=options.chart_column_measures) {
        view.measures=view.default_measures=options.chart_column_measures;
        view.reset=true;
    }
    if(view.follow!=options.follow_playback) {
        view.follow=options.follow_playback;
        view.reset=true;
    }
}
float last_time(const State& s) {
    return std::max({s.track.duration,s.track.preview?s.track.preview->last_time:0.f,1.f});
}
void label(ImDrawList* draw,ImVec2 pos,ImU32 color,const char* text) { draw->AddText(pos,color,text); }
void mark_region_at_mouse(const State& s,Session* commands,ImVec2 origin,float height,unsigned columns,float start) {
    const bool in=ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const bool out=ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right);
    if ((!in && !out) || !commands || !s.enabled || ImGui::GetIO().WantTextInput ||
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) return;
    const auto mouse=ImGui::GetIO().MousePos;
    const float offset=mouse.x-origin.x,column=std::floor(offset/column_width);
    const float road=offset-column*column_width;
    if (column<0 || column>=columns || road<gutter || road>gutter+track_width ||
        mouse.y<origin.y+24 || mouse.y>origin.y+height-20) return;
    const float fraction=(origin.y+height-20-mouse.y)/(height-44);
    const float time=static_cast<float>(s.track.preview->timing.seconds(start+(column+fraction)*view.measures));
    // Do not turn the unused top of the last column into an end-of-song mark.
    if (time<0.f || time>s.track.duration) return;
    view.drag=RegionDrag::None;
    view.suppress_drag=ImGui::IsMouseDown(ImGuiMouseButton_Left);
    commands->submit({s.generation,in?Action::SetIn:Action::SetOut,time});
    if (in && time<=view.out-.05f) view.in=time;
    if (!in && time>=view.in+.05f) view.out=time;
}
void update_region_drag(const State& s,Session* commands,ImVec2 origin,float height,unsigned columns,float start) {
    if (view.drag==RegionDrag::None) return;
    if (!commands || !s.enabled || !s.loop) { view.drag=RegionDrag::None; return; }
    if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseReleased(0)) { view.drag=RegionDrag::None; return; }
    const auto mouse=ImGui::GetIO().MousePos;
    const float column=std::clamp(std::floor((mouse.x-origin.x)/column_width),0.f,static_cast<float>(columns-1));
    const float fraction=std::clamp((origin.y+height-20-mouse.y)/(height-44),0.f,1.f);
    const auto& timing=s.track.preview->timing;
    const double coordinate=start+(column+fraction)*view.measures;
    const double delta=coordinate-view.anchor;
    float in=view.initial_in,out=view.initial_out;
    if (view.drag==RegionDrag::In) in=std::clamp(static_cast<float>(timing.seconds(timing.position(in)+delta)),0.f,out-.05f);
    else if (view.drag==RegionDrag::Out) out=std::clamp(static_cast<float>(timing.seconds(timing.position(out)+delta)),in+.05f,s.track.duration);
    else {
        const double length=timing.position(out)-timing.position(in);
        const double begin=std::clamp(timing.position(in)+delta,timing.position(0),
                                      std::max(timing.position(0),timing.position(s.track.duration)-length));
        in=std::clamp(static_cast<float>(timing.seconds(begin)),0.f,s.track.duration);
        out=std::clamp(static_cast<float>(timing.seconds(begin+length)),in,s.track.duration);
        if(out-in<.05f) { in=std::max(0.f,out-.05f); out=std::min(s.track.duration,in+.05f); }
    }
    if (in!=view.in || out!=view.out) {
        view.in=in; view.out=out;
        commands->submit({s.generation,Action::SetRegion,in,out});
    }
    if (!ImGui::IsMouseDown(0)) view.drag=RegionDrag::None;
}
void draw_region(ImDrawList* draw,const State& s,Session* commands,float x,float top,float bottom,float from,float to,bool last) {
    if (view.out<=view.in) return;
    const float right=x+track_width;
    const bool active=commands && s.enabled && s.loop;
    auto color=[&](int r,int g,int b,int alpha){return IM_COL32(r,g,b,active?alpha:alpha/4);};
    const auto& timing=s.track.preview->timing;
    const double axis_begin=timing.position(from),span=timing.position(to)-axis_begin;
    auto y=[&](float time){return bottom-static_cast<float>((timing.position(time)-axis_begin)/span)*(bottom-top);};
    auto owns=[&](float time){return time>=from && (time<to || (last && time<=to));};
    auto hit=[&](const char* id,ImVec2 p,ImVec2 size,RegionDrag kind) {
        ImGui::SetCursorScreenPos(p);
        ImGui::BeginDisabled(!active || view.suppress_drag);
        ImGui::InvisibleButton(id,size);
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(kind==RegionDrag::Move?ImGuiMouseCursor_ResizeAll:ImGuiMouseCursor_ResizeNS);
            if (view.drag==RegionDrag::None) ImGui::SetTooltip("%s",i18n::text(kind==RegionDrag::Move?"Drag to move selection":"Drag to resize selection"));
        }
        if (ImGui::IsItemActivated()) {
            view.drag=kind; view.initial_in=view.in; view.initial_out=view.out;
            view.anchor=static_cast<float>(axis_begin+std::clamp((bottom-ImGui::GetIO().MousePos.y)/(bottom-top),0.f,1.f)*span);
        }
        ImGui::EndDisabled();
    };
    // IN and OUT tabs sit on opposite sides so both remain accessible even
    // when the selected interval is shorter than a handle's pixel height.
    if (owns(view.in)) {
        const float py=y(view.in),tab=std::clamp(py-7,top,bottom-14);
        draw->AddLine({x,py},{right,py},color(71,220,182,230),2.f);
        draw->AddRectFilled({x-28,tab},{x-2,tab+14},color(34,164,145,255));
        label(draw,{x-24,tab+1},color(230,255,249,255),"IN");
        hit("region-in",{x-29,tab-2},{34,18},RegionDrag::In);
    }
    if (owns(view.out)) {
        const float py=y(view.out),tab=std::clamp(py-7,top,bottom-14);
        draw->AddLine({x,py},{right,py},color(239,185,82,230),2.f);
        draw->AddRectFilled({right+2,tab},{right+30,tab+14},color(181,122,48,255));
        label(draw,{right+5,tab+1},color(255,243,218,255),"OUT");
        hit("region-out",{right-5,tab-2},{36,18},RegionDrag::Out);
    }
    const float a=std::max(from,view.in),b=std::min(to,view.out);
    if (a>=b) return;
    const float high=y(b),low=y(a);
    draw->AddRectFilled({x,high},{right,low},color(55,194,160,44));
    draw->AddLine({x,high},{x,low},color(91,216,180,170));
    draw->AddLine({right,high},{right,low},color(91,216,180,170));
    const float middle=(high+low)*.5f;
    for (float offset:{-2.f,2.f}) draw->AddLine({x+track_width*.5f-7,middle+offset},
        {x+track_width*.5f+7,middle+offset},color(140,243,213,230),1.5f);
    if (low-high>4) hit("region-body",{x+7,high+1},{track_width-14,low-high-2},RegionDrag::Move);
}
void draw_column(ImDrawList* draw,const State& s,Session* commands,float x,float top,float bottom,float from,float to,bool last,int background_alpha) {
    const auto& chart=*s.track.preview;
    const float right=x+track_width,cell=track_width/4.f;
    const auto& timing=s.track.preview->timing;
    const double axis_begin=timing.position(from),span=timing.position(to)-axis_begin;
    auto y=[&](float time){return bottom-static_cast<float>((timing.position(time)-axis_begin)/span)*(bottom-top);};
    auto owns=[&](float time){return time>=from && (time<to || (last && time<=to));};
    draw->AddRectFilled({x,top},{right,bottom},IM_COL32(5,7,9,background_alpha));
    draw->AddRectFilled({x-4,top},{x+5,bottom},IM_COL32(19,83,108,45));
    draw->AddRectFilled({right-5,top},{right+4,bottom},IM_COL32(103,30,77,45));
    for (unsigned lane=0;lane<=4;++lane) {
        const auto color=lane==0?IM_COL32(55,141,167,145):lane==4?IM_COL32(164,57,117,145):IM_COL32(138,143,146,82);
        draw->AddLine({x+lane*cell,top},{x+lane*cell,bottom},color);
    }
    for(const auto& beat:chart.beats) if(owns(beat.time))
        draw->AddLine({x,y(beat.time)},{right,y(beat.time)},IM_COL32(109,125,135,38));
    for (const auto& measure:chart.measures) if (owns(measure.time)) {
        const float py=y(measure.time);
        draw->AddLine({x,py},{right,py},IM_COL32(123,136,143,78));
        char text[24]; sprintf_s(text,"%02u",measure.index+1);
        label(draw,{x-29,std::clamp(py-6,top,bottom-13)},IM_COL32(167,174,179,255),text);
    }
    draw->PushClipRect({x-12,top},{right+12,bottom},true);
    // FX occupies a half-width lane beneath the four BT lanes, as in the
    // reference chart. Draw FX first so simultaneous BT notes remain legible.
    for (unsigned layer=0;layer<2;++layer) for (const auto& note:chart.notes) {
        if (note.start>to) break;
        if (note.end<from || (!last && note.start>=to) || ((note.lane>=4)!=(layer==0))) continue;
        const bool fx=note.lane>=4,hold=note.end>note.start;
        const float left=x+(fx?(note.lane-4)*track_width*.5f:note.lane*cell)+1;
        const float width=(fx?track_width*.5f:cell)-2;
        const auto color=fx?orange:white;
        if (hold) {
            const float a=y(std::min(to,note.end)),b=y(std::max(from,note.start));
            const auto fill=fx?IM_COL32(211,139,46,120):IM_COL32(211,223,226,110);
            draw->AddRectFilled({left,a},{left+width,b},fill);
            draw->AddLine({left+1,a},{left+1,b},fx?IM_COL32(236,171,74,105):IM_COL32(231,239,242,85));
            if (owns(note.end)) draw->AddLine({left,y(note.end)},{left+width,y(note.end)},color,1.5f);
        }
        if (owns(note.start)) draw->AddRectFilled({left,y(note.start)-1.2f},{left+width,y(note.start)+1.2f},color);
    }
    for (const auto& laser:chart.lasers) {
        const auto& nodes=laser.points;
        if (nodes.empty() || nodes.back().time<from || nodes.front().time>to) continue;
        const auto color=laser.side==0?blue:pink;
        auto first=std::lower_bound(nodes.begin(),nodes.end(),from,[](const auto& point,float time){return point.time<time;});
        if (first!=nodes.begin()) --first;
        for (auto it=first;it!=nodes.end() && it+1!=nodes.end();++it) {
            const auto& a=*it; const auto& b=*(it+1);
            if (a.time>to || (!last && a.time>=to)) break;
            if (b.time<from) continue;
            const float t0=std::max(from,a.time),t1=std::min(to,b.time);
            const float delta=b.time-a.time;
            // Clip in song time before building the ribbon. Same-time points
            // intentionally retain a horizontal slam across the shared road.
            const float v0=delta>0?a.position+(b.position-a.position)*(t0-a.time)/delta:a.position;
            const float v1=delta>0?a.position+(b.position-a.position)*(t1-a.time)/delta:b.position;
            ImVec2 previous{x+v0*track_width,y(t0)};
            const auto& knots=timing.points();
            auto knot=std::upper_bound(knots.begin(),knots.end(),t0,[](float t,const MusicalTimeline::Point& p){return t<p.time;});
            for(;delta>0 && knot!=knots.end() && knot->time<t1;++knot) {
                const auto t=static_cast<float>(knot->time);
                const ImVec2 next{x+(a.position+(b.position-a.position)*(t-a.time)/delta)*track_width,y(t)};
                draw->AddLine(previous,next,color,6.f); previous=next;
            }
            draw->AddLine(previous,{x+v1*track_width,y(t1)},color,6.f);
        }
        const auto& begin=nodes.front(); const auto& end=nodes.back();
        if (owns(begin.time)) {
            const float px=x+begin.position*track_width,py=y(begin.time);
            draw->AddLine({px-4,py+9},{px,py+3},color,1.2f);
            draw->AddLine({px,py+3},{px+4,py+9},color,1.2f);
        }
        if (owns(end.time)) {
            const float px=x+end.position*track_width,py=y(end.time);
            draw->AddRectFilled({px-3,py-9},{px+3,py+2},color);
        }
    }
    draw->PopClipRect();
    draw_region(draw,s,commands,x,top,bottom,from,to,last);
    if (owns(s.position)) {
        const float py=std::clamp(y(s.position),top+2,bottom-2);
        draw->AddLine({x-6,py},{right+6,py},gold,2.f);
        draw->AddTriangleFilled({x-14,py-4},{x-14,py+4},{x-7,py},gold);
        label(draw,{right+8,std::clamp(py-6,top,bottom-13)},gold,i18n::text("NOW"));
    }
}
}
void draw_chart_preview(const State& s,Session* commands,float background_opacity) {
    if (!s.playing) { view.drag=RegionDrag::None; ImGui::TextWrapped("%s",i18n::text("The chart appears when a track starts.")); return; }
    if (!s.track.preview || !s.track.preview->error.empty()) {
        ImGui::TextWrapped(i18n::text("Chart preview unavailable: %s"),i18n::text(s.track.preview?s.track.preview->error.c_str():"no chart snapshot")); return;
    }
    if(s.track.preview->timing.empty()) { ImGui::TextWrapped("%s",i18n::text("Musical timing unavailable")); return; }
    sync_view(s);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) view.suppress_drag=false;
    if (view.drag==RegionDrag::None) { view.in=s.in; view.out=s.out; }
    const auto& chart=*s.track.preview;
    const auto& timing=chart.timing;
    const float duration=last_time(s);
    const float start=std::floor(static_cast<float>(timing.position(chart.first_time))+.0001f);
    const float span=std::max(.0001f,static_cast<float>(timing.position(duration))-start);
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    ImGui::SeparatorText(i18n::text("Chart View"));
    ImGui::PopStyleColor();
    const float viewport_width=std::max(1.f,ImGui::GetContentRegionAvail().x-2*ImGui::GetStyle().WindowPadding.x);
    const float fit_width=ImGui::CalcTextSize(i18n::text("Fit")).x+2*ImGui::GetStyle().FramePadding.x;
    ImGui::SetNextItemWidth(std::max(40.f,ImGui::GetContentRegionAvail().x-fit_width-ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::SliderInt("##chart-zoom",&view.measures,1,12,i18n::text("%d measures / column"),
        ImGuiSliderFlags_Logarithmic|ImGuiSliderFlags_AlwaysClamp)) view.reset=true;
    ImGui::SameLine();
    if (ImGui::Button(i18n::label("Fit"))) { view.measures=static_cast<int>(std::ceil(span/std::max(1.f,std::floor(viewport_width/column_width)))); view.reset=true; }
    const float footer_height=ImGui::GetTextLineHeightWithSpacing();
    const float height=std::max(120.f,ImGui::GetContentRegionAvail().y-footer_height);
    ImGui::BeginChild("chart-timeline",{0,height},ImGuiChildFlags_Borders,
        ImGuiWindowFlags_AlwaysHorizontalScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    const float visible=std::max(1.f,ImGui::GetContentRegionAvail().x);
    const float content_height=std::max(80.f,ImGui::GetContentRegionAvail().y-2.f);
    const auto p=ImGui::GetCursorScreenPos();
    const auto count=static_cast<unsigned>(std::max(1.f,std::ceil(span/view.measures-0.00001f)));
    const float width=std::max(visible,count*column_width);
    const auto current=static_cast<unsigned>(std::clamp(std::floor((static_cast<float>(timing.position(s.position))-start)/view.measures),0.f,static_cast<float>(count-1)));
    const float column_x=current*column_width;
    float scroll=ImGui::GetScrollX(),desired=view.first_frame?0.f:scroll;
    // Browsing and selection edits may defer following while the pointer is held,
    // but only the user's Option preference can turn following off.
    if (view.drag==RegionDrag::None && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && view.follow &&
        (view.reset || column_x<scroll || column_x+column_width>scroll+visible)) desired=column_x;
    if (view.drag!=RegionDrag::None && ImGui::IsMouseDown(0)) {
        const auto mouse=ImGui::GetIO().MousePos;
        const float edge=p.x+scroll;
        if (mouse.y>=p.y && mouse.y<=p.y+content_height) {
            if (mouse.x<edge+10) desired-=240.f*ImGui::GetIO().DeltaTime;
            if (mouse.x>edge+visible-10) desired+=240.f*ImGui::GetIO().DeltaTime;
        }
    }
    desired=std::clamp(desired,0.f,std::max(0.f,width-visible));
    if (std::abs(desired-scroll)>.5f || view.reset) ImGui::SetScrollX(desired);
    view.reset=false; view.first_frame=false;
    mark_region_at_mouse(s,commands,p,content_height,count,start);
    update_region_drag(s,commands,p,content_height,count,start);
    auto* draw=ImGui::GetWindowDrawList();
    const int background_alpha=static_cast<int>(std::clamp(background_opacity,0.f,1.f)*255.f+.5f);
    draw->AddRectFilled({p.x+scroll,p.y},{p.x+scroll+visible,p.y+content_height},IM_COL32(3,5,8,background_alpha));
    const auto first=static_cast<unsigned>(std::max(0.f,std::floor(scroll/column_width)));
    const auto end=std::min(count,static_cast<unsigned>(std::ceil((scroll+visible)/column_width)));
    for (auto column=first;column<end;++column) {
        const float a=start+column*view.measures;
        // The final partial column keeps the same time scale as its neighbors.
        const float left=p.x+column*column_width;
        const float top=p.y+24,bottom=p.y+content_height-20;
        char text[48]; sprintf_s(text,i18n::text("M %.0f - %.0f"),a+1,a+view.measures);
        label(draw,{left+8,p.y+2},IM_COL32(153,166,181,255),text);
        ImGui::PushID(static_cast<int>(column));
        draw_column(draw,s,commands,left+gutter,top,bottom,static_cast<float>(timing.seconds(a)),static_cast<float>(timing.seconds(a+view.measures)),column+1==count,background_alpha);
        ImGui::PopID();
        sprintf_s(text,i18n::text("M %.0f"),a+1); label(draw,{left+gutter,bottom+5},IM_COL32(117,133,150,255),text);
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({width,content_height});
    ImGui::EndChild();
    ImGui::TextDisabled("%s",i18n::text("Double-click: Left = IN / Right = OUT."));
}
}
