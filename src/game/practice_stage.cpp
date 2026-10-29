#include "game/practice_stage.hpp"
#include "native/profile.hpp"
#include <algorithm>
#include <cstring>
namespace training {
std::size_t stage_stride() { return native::result_layout().stride; }
namespace {
template<class T> T& at(std::uintptr_t p,std::size_t offset) { return *reinterpret_cast<T*>(p+offset); }
std::uintptr_t last_record(std::uintptr_t data) {
    if (!data) return 0;
    const auto count=at<int>(data,32);
    const auto begin=at<std::uintptr_t>(data,8),end=at<std::uintptr_t>(data,16);
    if (count<=0 || !begin || end<begin || static_cast<std::uintptr_t>(count)>(end-begin)/stage_stride()) return 0;
    return begin+(count-1)*stage_stride();
}
}
PracticeStageRecord::PracticeStageRecord(unsigned ordinal,unsigned music,unsigned difficulty,
                                       std::span<const std::byte,236> options) {
    const auto p=address();
    const auto& layout=native::result_layout();
    std::memcpy(bytes_.data()+4,options.data(),options.size());
    at<unsigned>(p,0)=5; // no scored result
    at<unsigned>(p,layout.field(244))=difficulty;
    at<unsigned>(p,layout.field(1700))=ordinal;
    const auto category=std::to_integer<unsigned>(options[59]);
    at<unsigned>(p,layout.field(1704))=category>=1 && category<=8?category-1:std::min(ordinal,3u);
    at<unsigned>(p,layout.field(1708))=music;
    at<unsigned>(p,layout.tag)=practice_stage_tag;
    if(layout.challenge_present) at<std::uint8_t>(p,layout.challenge_present)=0;
    at<std::uint8_t>(p,layout.excluded)=1; // exclude from native pending-track serialization
    // Borrowed EMPTY native containers. Append deep-copies them with the game's
    // allocator; no plugin allocation is ever owned/freed by the game.
    const auto sentinel=reinterpret_cast<std::uintptr_t>(sentinel_.data());
    sentinel_[0]=sentinel_[1]=sentinel;
    buckets_.fill(sentinel);
    at<float>(p,layout.field(808))=1.f;
    at<std::uintptr_t>(p,layout.field(816))=sentinel;
    at<std::uintptr_t>(p,layout.field(832))=reinterpret_cast<std::uintptr_t>(buckets_.data());
    at<std::uintptr_t>(p,layout.field(840))=at<std::uintptr_t>(p,layout.field(848))=reinterpret_cast<std::uintptr_t>(buckets_.data()+buckets_.size());
    at<unsigned>(p,layout.field(864))=7;
    at<unsigned>(p,layout.field(868))=8;
    for (unsigned i=0;i<3;++i) at<unsigned>(p,layout.field(872)+276*i)=5;
}
bool remember_practice_selection(std::uintptr_t image,std::uintptr_t world) {
    __try {
        const auto& a=native::addresses();
        if (!image || !world || !a.player_settings || !a.player_settings_vtable ||
            at<std::uintptr_t>(world,0)!=image+a.world_vtable) return false;
        const auto settings=at<std::uintptr_t>(image,a.player_settings);
        if (!settings || at<std::uintptr_t>(settings,0)!=image+a.player_settings_vtable) return false;
        const auto music=at<unsigned>(world,native::world_layout().music);
        const auto difficulty=at<unsigned>(world,native::world_layout().music+4);
        // The native result collector writes last_music_id here and maps the
        // difficulty to 1..5, or 0 for any other value. These are UI settings,
        // not score, reward, or result records.
        at<unsigned>(settings,44)=music;
        at<unsigned>(settings,48)=difficulty>=1 && difficulty<=5?difficulty:0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool is_practice_stage(std::uintptr_t record) {
    const auto& layout=native::result_layout();
    return at<unsigned>(record,layout.tag)==practice_stage_tag &&
        (!layout.challenge_present || at<std::uint8_t>(record,layout.challenge_present)==0);
}
bool append_practice_stage(std::uintptr_t image,std::uintptr_t data,const PracticeStageRecord& record) {
    const auto before=at<int>(data,32);
    if (before<0) return false;
    using Append=void(__fastcall*)(std::uintptr_t,std::uintptr_t);
    reinterpret_cast<Append>(image+native::addresses().append_result)(data,record.address());
    return at<int>(data,32)==before+1 &&
        is_practice_stage(at<std::uintptr_t>(data,8)+static_cast<std::uintptr_t>(before)*stage_stride());
}
std::int64_t serialize_practice_safe(std::uintptr_t image,std::uintptr_t data,std::uintptr_t xml,NativeSerialize original) {
    const auto count=at<int>(data,32);
    const auto begin=at<std::uintptr_t>(data,8),end=at<std::uintptr_t>(data,16);
    if (count<=0) return 1;
    if (!begin || end<begin || static_cast<std::uintptr_t>(count)>(end-begin)/stage_stride()) return 1;
    for (int i=0;i<count;++i) {
        const auto record=begin+static_cast<std::uintptr_t>(i)*stage_stride();
        if (is_practice_stage(record)) at<std::uint8_t>(record,native::result_layout().excluded)=1;
    }
    // The play_id=0 branch ignores the exclusion flag and serializes the
    // last slot. Skip that invocation when its last slot is practice.
    if (at<int>(image,native::addresses().play_id)==0 && is_practice_stage(begin+(count-1)*stage_stride())) return 1;
    return original(data,xml);
}
bool can_continue_practice_safe(std::uintptr_t image,std::uintptr_t data,NativeContinue original) {
    // PREMIUM TIME uses a real-time stopwatch, not a per-credit track limit.
    // Always delegate to the trampoline, including before the first track.
    if (at<int>(image,native::addresses().game_mode)==6) return original(data);
    const auto last=last_record(data);
    if (!last || !is_practice_stage(last)) return original(data);
    const auto count=at<int>(data,32);
    const auto begin=at<std::uintptr_t>(data,8);
    // Practice consumes a stage without inventing a clear or failed gauge.
    // It may continue within the native stage budget, but grants no extra track.
    const auto first_mode=at<std::uint8_t>(begin,97);
    const auto limit=first_mode==1 || first_mode==18?
        (native::result_layout().fixed_timed_limit?native::result_layout().fixed_timed_limit:
         reinterpret_cast<unsigned(__fastcall*)()>(image+native::addresses().timed_stage_limit)()):
        reinterpret_cast<unsigned(__fastcall*)(int)>(image+native::addresses().stage_limit)(at<int>(image,native::addresses().game_mode));
    return static_cast<unsigned>(count)<limit;
}
unsigned decide_practice_flow(std::uintptr_t image,std::uintptr_t flow,int state,std::uintptr_t argument,
                             NativeContinue continuation,NativeFlow original) {
    if (state==43 && argument==18) {
        const auto data=reinterpret_cast<std::uintptr_t(__fastcall*)()>(image+native::addresses().stage_data)();
        const auto last=last_record(data);
        if (last && is_practice_stage(last) &&
            !can_continue_practice_safe(image,data,continuation))
            // Last paid stage: use native post-result routing (total/session
            // end) without opening the per-song result scene or granting extra.
            return original(flow,16,0);
    }
    return original(flow,state,argument);
}
}
