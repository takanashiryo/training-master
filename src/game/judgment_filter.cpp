#include "game/judgment_filter.hpp"
#include <windows.h>
#include <cmath>
namespace training::judgment_filter {
namespace {
template<class T> T read(Ptr p,std::size_t offset) { return *reinterpret_cast<const T*>(p+offset); }
struct Points { Ptr begin,end; };
struct Times { float note{},press{}; bool has_note{},has_press{}; };
Times event_times(Ptr note,Ptr result,float judge_time,float song_time) {
    Times times{};
    __try {
        if(!note || !result) return times;
        const auto object=read<Ptr>(note,0);
        if(!object) return times;
        const auto method=read<Ptr>(read<Ptr>(object,8),16);
        const auto points=reinterpret_cast<Ptr(__fastcall*)(Ptr)>(method)(object+8);
        if(!points) return times;
        const auto bounds=read<Points>(points,0);
        const int index=read<int>(result,16);
        if(!bounds.begin || bounds.end<bounds.begin || (bounds.end-bounds.begin)%48 ||
           index<0 || static_cast<Ptr>(index)>=(bounds.end-bounds.begin)/48) return times;
        times.note=read<float>(bounds.begin+static_cast<Ptr>(index)*48,0);
        times.has_note=std::isfinite(times.note);
        const auto kind=read<unsigned>(result,0);
        const bool button_press=kind>=1 && kind<=9 && read<unsigned char>(result,24)==1 &&
            read<unsigned char>(result,32)==1 && read<unsigned>(result,28)==1;
        const auto delta=read<float>(result,8);
        if(times.has_note && button_press && std::isfinite(delta) && std::fabs(delta)<1.f &&
           std::isfinite(judge_time) && std::isfinite(song_time)) {
            // JudgeTimer includes the player's timing offset. Compare the input
            // on the same song clock as the IN/OUT marks, not JudgeTimer's clock.
            times.press=times.note+delta-(judge_time-song_time);
            times.has_press=std::isfinite(times.press);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return {}; }
    return times;
}
bool skip(Ptr context,Ptr output,Ptr player,Ptr vtable,Failed failed) {
    __try {
        if(!context || !output || !player || !vtable || !failed || read<Ptr>(context,0)!=player ||
           read<Ptr>(player,0)!=vtable) return false;
        const auto secondary=read<Ptr>(player,8);
        if(!secondary || read<Ptr>(secondary,136)!=reinterpret_cast<Ptr>(failed)) return false;
        *reinterpret_cast<unsigned char*>(output)=failed(player+8)?1:0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
bool allowed(Region region,float song_time,float note_time,bool have_note,float press_time,bool have_press) {
    if(!region.active || !std::isfinite(region.in) || !std::isfinite(region.out) || region.in>region.out) return true;
    auto inside=[&](float time) { return std::isfinite(time) && time>=region.in && time<=region.out; };
    return inside(song_time) && (!have_note || inside(note_time)) && (!have_press || inside(press_time));
}
Ptr dispatch(Event original,Ptr context,Ptr output,Ptr note,Ptr result,float judge_time,
             float song_time,Region region,Ptr player,Ptr vtable,Failed failed,bool* suppressed) {
    if(suppressed) *suppressed=false;
    if(region.active) {
        const auto times=event_times(note,result,judge_time,song_time);
        if(!allowed(region,song_time,times.note,times.has_note,times.press,times.has_press) &&
           skip(context,output,player,vtable,failed)) {
            if(suppressed) *suppressed=true;
            return output;
        }
    }
    return original(context,output,note,result,judge_time);
}
}
