#include "core/chart.hpp"
#include "native/profile.hpp"
#include <windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
using namespace training;
namespace {
using Ptr=std::uintptr_t;
std::vector<std::pair<Ptr,std::size_t>> regions;
bool read(Ptr p,void* to,std::size_t n) {
    for (const auto [start,size]:regions) if (p>=start && p-start<=size && n<=size-(p-start)) {
        std::memcpy(to,reinterpret_cast<const void*>(p),n); return true;
    }
    return false;
}
template<class T,std::size_t N> void put(std::array<std::byte,N>& a,std::size_t offset,T v) { std::memcpy(a.data()+offset,&v,sizeof(v)); }
template<std::size_t N> void bounds(std::array<std::byte,N>& a,std::size_t offset,Ptr start,std::size_t size) {
    put(a,offset,start); put(a,offset+8,start+size); put(a,offset+16,start+size);
}
template<std::size_t N> Ptr add(std::array<std::byte,N>& a) {
    const auto ptr=reinterpret_cast<Ptr>(a.data()); regions.emplace_back(ptr,N); return ptr;
}
void check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
}
int wmain(int argc,wchar_t** argv) {
    HMODULE module{};
    try {
        if(argc==2) {
            module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
            check(module!=nullptr,"native mapping failed"); std::string diagnostic;
            check(native::validate(module,diagnostic),diagnostic.c_str());
            const Ptr base=reinterpret_cast<Ptr>(module),table=base+native::addresses().fumen_data_vtable;
            using Getter=Ptr(__fastcall*)(Ptr);
            std::array<std::byte,160> object{};
            for(unsigned slot=4;slot<=6;++slot) {
                const auto getter=reinterpret_cast<Getter>(reinterpret_cast<const Ptr*>(table)[slot]);
                const Ptr instance=reinterpret_cast<Ptr>(object.data());
                check(getter(instance)==instance+80+(slot-4)*24,"native BPM, measure and beat accessors must match copied headers");
            }
            std::cout<<"Native musical timing ABI: BPM +80, measures +104, beats +128 verified\n";
        }
        std::array<std::byte,160> data{};
        std::array<std::byte,96> chips{};
        std::array<std::byte,72> holds{};
        std::array<std::byte,176> lasers{};
        std::array<std::byte,144> left{},right{};
        std::array<std::byte,16> measures{};
        std::array<std::byte,20> tempos{};
        std::array<std::byte,64> beats{};
        const auto root=add(data);
        bounds(data,8,add(chips),chips.size()); bounds(data,32,add(holds),holds.size());
        bounds(data,56,add(lasers),lasers.size()); bounds(data,104,add(measures),measures.size());
        put(chips,4,3.f); put(chips,8,1u); put(chips,52,1.f); put(chips,56,6u);
        put(holds,4,2.f); put(holds,8,8.f); put(holds,12,5u);
        put(lasers,4,1u); put(lasers,8,1.f); bounds(lasers,16,add(left),left.size());
        put(lasers,92,2u); put(lasers,96,2.f); bounds(lasers,104,add(right),right.size());
        for (unsigned i=0;i<3;++i) {
            put(left,i*48,4.f+(i==2?0.f:static_cast<float>(i))); put(left,i*48+4,i*.5f);
            put(right,i*48,4.f+i); put(right,i*48+4,1.f-i*.5f);
        }
        // Keep the final two timestamps equal to preserve a slam exactly.
        put(left,96,5.f); put(left,112,std::uint8_t{1});
        put(measures,0,0.f); put(measures,8,2.f); put(measures,12,1u);
        bounds(data,80,add(tempos),tempos.size()); put(tempos,4,120.f);
        bounds(data,128,add(beats),beats.size());
        for(unsigned i=0;i<8;++i) { put(beats,i*8,i*.5f); put(beats,i*8+4,i); }
        const auto chart=read_native_chart(root,read);
        check(chart->error.empty(),"valid native chart must decode");
        check(chart->notes.size()==3 && chart->notes[0].start==1 && chart->notes[0].lane==5,
              "chips and holds must merge in seconds with all six lanes");
        check(chart->notes[1].start==2 && chart->notes[1].end==8 && chart->notes[1].lane==4,
              "hold endpoint must be read from its own layout");
        check(chart->lasers.size()==2 && chart->lasers[1].side==1 && chart->lasers[1].width==2,
              "88-byte laser records must remain separate from holds");
        check(chart->lasers[0].points[1].time==chart->lasers[0].points[2].time && chart->lasers[0].points[2].wide,
              "equal-time slam knots and extended-range flags must survive");
        check(chart->measures.size()==2 && chart->measures[1].time==2 && chart->measures[1].index==1,"measure timing is in song seconds");
        check(chart->beats.size()==8 && chart->tempos.size()==1 && std::abs(chart->timing.position(1)-.5)<.0001,
              "native beat and BPM records must build an owned musical map");
        put(tempos,4,std::numeric_limits<float>::quiet_NaN());
        check(!read_native_chart(root,read)->error.empty(),"malformed timing must reject preview safely");
        put(tempos,4,120.f);
        put(chips,8,7u);
        check(!read_native_chart(root,read)->error.empty(),"unknown lanes must reject snapshot");
        put(chips,8,1u); put(holds,8,std::numeric_limits<float>::quiet_NaN());
        check(!read_native_chart(root,read)->error.empty(),"nonfinite time must reject snapshot");
        put(holds,8,8.f); put(right,96,1.f);
        check(!read_native_chart(root,read)->error.empty(),"backward laser points must reject snapshot");
        put(right,96,6.f); bounds(data,56,0x1000,88*100001);
        const auto corrupt=read_native_chart(root,read);
        check(!corrupt->error.empty() && corrupt->notes.empty(),"oversize arrays fail without partial chart");
        bounds(data,56,reinterpret_cast<Ptr>(lasers.data()),lasers.size());
        put(lasers,16,Ptr{0x1000}); put(lasers,24,Ptr{0x1030}); put(lasers,32,Ptr{0x1030});
        check(!read_native_chart(root,read)->error.empty(),"unreadable nested native memory must be contained");
        check(chart->notes[0].lane==5 && chart->lasers[0].points.size()==3,"published snapshot must own its values");
        check(!read_native_chart(0,read)->error.empty(),"missing chart must be reported");
        std::cout<<"Native chart snapshot scenarios passed\n";
        if(module) FreeLibrary(module);
        return 0;
    } catch (const std::exception& e) { if(module) FreeLibrary(module); std::cerr<<e.what()<<'\n'; return 1; }
}
