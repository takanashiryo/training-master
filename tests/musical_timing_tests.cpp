#include "core/musical_timing.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace training;
namespace {
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void near(double a,double b,const char* message) { check(std::abs(a-b)<.00001,message); }
}
int main() {
    try {
        const std::vector<ChartLine> measures{{0,0},{1.125f,1},{1.875f,2}};
        const std::vector<float> times{0,.375f,.625f,.875f,1.125f,1.375f,1.625f,1.875f,2.375f,2.875f,3.375f};
        std::vector<ChartLine> beats;
        for(unsigned i=0;i<times.size();++i) beats.push_back({times[i],i});
        const std::vector<ChartTempo> tempos{{0,120},{.25f,240},{1.875f,120}};
        const auto map=MusicalTimeline::build(measures,beats,tempos);
        near(map.position(.25),.125,"BPM change inside the first beat must split musical spacing");
        near(map.position(.375),.25,"first beat must occupy one quarter of the 4/4 measure");
        near(map.position(1.125),1,"accelerating first measure must end at its native boundary");
        near(map.position(1.375),1.+1./3.,"3/4 beats must subdivide their measure into thirds");
        near(map.position(1.875),2,"time signature change must preserve the native next measure");
        near(map.position(2.875),2.5,"slower BPM must still place half a measure halfway up the grid");
        near(map.seconds(3),3.875,"final bar must extend by its BPM rather than stretch to song duration");
        for(double t=-.1;t<4;t+=.013) near(map.seconds(map.position(t)),t,"drawing and picking mappings must round-trip");
        const auto width=map.position(1.5)-map.position(.625);
        near(map.seconds(map.position(.625)+1),1.5,"whole selection move must follow musical coordinates");
        near(map.seconds(map.position(.625)+1+width),2.875,"selection must preserve musical length across BPM and signature changes");
        const std::vector<ChartLine> stop_measures{{0,0}},stop_beats{{0,0},{1,1},{1.5f,2},{2,3}};
        const std::vector<ChartTempo> stop_tempos{{0,120},{.25f,0},{.75f,120}};
        const auto stop=MusicalTimeline::build(stop_measures,stop_beats,stop_tempos);
        near(stop.position(.25),stop.position(.75),"zero BPM interval must not advance the musical axis");
        near(stop.seconds(stop.position(.5)),.75,"picking a stopped beat must consistently choose its resume time");
        for(int bad=0;bad<3;++bad) {
            bool rejected=false;
            try {
                auto invalid=beats;
                if(bad==0) invalid[1].time=-1;
                else if(bad==1) invalid[1].index=0;
                else invalid.clear();
                MusicalTimeline::build(measures,invalid,tempos);
            } catch(const std::exception&) { rejected=true; }
            check(rejected,"malformed musical metadata must fail without a seconds-based substitute");
        }
        std::cout<<"BPM, within-beat changes, time signatures, final partial bar, stopped beats and inverse picking passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
