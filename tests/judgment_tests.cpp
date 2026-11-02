#include "core/judgment.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <cstring>
void check(bool b,const char* message) { if(!b) throw std::runtime_error(message); }
int main() {
    using namespace training::judgment;
    try {
        set_available(true); reset();
        for(unsigned id=1;id<=10;++id) record(id,0.f,false);
        auto s=snapshot();
        check(s.counts==std::array<std::uint64_t,7>{1,1,1,3,1,1,2},"native judgment categories");
        record(4,-.010f,true);
        s=snapshot();
        check(s.average_ms<0 && std::abs(s.absolute_average_ms-10.)<.001,"early timing has negative signed mean and positive overall magnitude");
        record(6,.030f,true);
        record(10,.200f,true); record(5,0.f,false);
        record(4,std::numeric_limits<float>::quiet_NaN(),true);
        s=snapshot();
        check(s.timed_hits==2 && std::abs(s.average_ms-10.)<.001 && std::abs(s.absolute_average_ms-20.)<.001,"signed and absolute means use real button timings only");
        std::array<unsigned char,40> native{};
        unsigned kind=3,note_type=1; float offset=-.012f;
        std::memcpy(native.data(),&kind,4); std::memcpy(native.data()+8,&offset,4);
        std::memcpy(native.data()+28,&note_type,4); native[24]=native[32]=1;
        observe_native(reinterpret_cast<std::uintptr_t>(native.data()));
        s=snapshot();
        check(s.counts[2]==2 && s.timed_hits==3 && std::abs(s.average_ms-8./3.)<.001 && std::abs(s.absolute_average_ms-52./3.)<.001,
              "native press metadata and signed delta are decoded without changing the record");
        native[32]=2; observe_native(reinterpret_cast<std::uintptr_t>(native.data()));
        check(snapshot().timed_hits==3,"non-button ticks do not affect average press timing");
        observe_native(0); observe_native(1);
        reset();
        record(4,-.010f,true,10.f);
        record(6,.030f,true,20.f);
        restore(15.f);
        s=snapshot();
        check(s.counts[3]==1 && s.timed_hits==1 && std::abs(s.average_ms+10.)<.001,
              "rewinding between two hits restores the earlier judgment and timing");
        check(std::abs(s.absolute_average_ms-10.)<.001,"rewind restores absolute timing mean");
        restore(30.f);
        check(snapshot().counts[3]==1,"forward seek cannot resurrect discarded judgments");
        record(2,.020f,true,20.f);
        check(snapshot().counts[1]==1 && snapshot().counts[3]==1 && snapshot().timed_hits==2,
              "replaying a note replaces its discarded judgment without accumulation");
        restore(20.f);
        check(snapshot().timed_hits==2,"same-position refresh preserves judgments at the cursor");
        set_colors_swapped(true); set_s_critical_enabled(false);
        restore(19.f); restore(15.f);
        s=snapshot();
        check(s.timed_hits==1 && s.colors_swapped && !s.s_critical_enabled && s.available,
              "reverse frames restore history while preserving live display settings");
        restore(9.f);
        check(snapshot().timed_hits==0 && snapshot().counts==std::array<std::uint64_t,7>{},
              "rewind before the first hit clears all judgment data");
        restore(std::numeric_limits<float>::quiet_NaN());
        record(10,0.f,false,12.f); restore(0.f);
        check(snapshot().counts[6]==0,"rewind also removes automatic misses");
        record(1,0.f,false,10.f); reset(); restore(20.f);
        check(snapshot().counts==std::array<std::uint64_t,7>{},"track reset discards judgment history");
        reset(); record(4,-.010f,true); record(6,.010f,true); s=snapshot();
        check(std::abs(s.average_ms)<.001 && std::abs(s.absolute_average_ms-10.)<.001,
              "equal early and late timings cancel in signed mean but retain overall magnitude");
        set_colors_swapped(true); reset(); s=snapshot();
        check(s.colors_swapped,"reset must preserve the current game color setting");
        set_s_critical_enabled(false); reset();
        check(!snapshot().s_critical_enabled,"reset must preserve the effective S-CRITICAL setting");
        set_s_critical_enabled(true);
        set_colors_swapped(false);
        check(s.available && s.timed_hits==0 && s.average_ms==0 && s.absolute_average_ms==0 && s.counts==std::array<std::uint64_t,7>{},"reset clears data and preserves availability");
        std::cout<<"Judgment category, mean and reset tests passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
