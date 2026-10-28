#include "core/musical_timing.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace training {
namespace {
struct Clock {
    struct Point { double time,bpm,beat; };
    std::vector<Point> points;
    explicit Clock(std::span<const ChartTempo> tempos) {
        for(const auto& t:tempos) {
            if(!std::isfinite(t.time) || !std::isfinite(t.bpm) || t.bpm<0 || t.bpm>100000 ||
               (!points.empty() && t.time<points.back().time)) throw std::runtime_error("Invalid musical tempo map");
            const double beat=points.empty()?0:at(t.time);
            if(!points.empty() && t.time==points.back().time) points.back()={t.time,t.bpm,beat};
            else points.push_back({t.time,t.bpm,beat});
        }
        if(points.empty()) throw std::runtime_error("Musical timing unavailable");
    }
    double at(double time) const {
        auto it=std::upper_bound(points.begin(),points.end(),time,[](double t,const Point& p){return t<p.time;});
        if(it!=points.begin()) --it;
        return it->beat+(time-it->time)*it->bpm/60.;
    }
    double time_at(double beat) const {
        auto it=std::upper_bound(points.begin(),points.end(),beat,[](double b,const Point& p){return b<p.beat;});
        if(it!=points.begin()) --it;
        if(it->bpm<=0) throw std::runtime_error("Invalid musical tempo map");
        return it->time+(beat-it->beat)*60./it->bpm;
    }
};
void validate(std::span<const ChartLine> lines,bool strict) {
    for(std::size_t i=0;i<lines.size();++i)
        if(!std::isfinite(lines[i].time) || lines[i].index>1000000 ||
           (i && (lines[i].index<=lines[i-1].index || (strict?lines[i].time<=lines[i-1].time:lines[i].time<lines[i-1].time))))
            throw std::runtime_error("Invalid musical line map");
}
}
MusicalTimeline MusicalTimeline::build(std::span<const ChartLine> measures,std::span<const ChartLine> beats,
                                        std::span<const ChartTempo> tempos) {
    if(measures.empty() || beats.empty()) throw std::runtime_error("Musical timing unavailable");
    validate(measures,true); validate(beats,true);
    const Clock clock(tempos);
    std::vector<Point> anchors;
    for(std::size_t m=0;m<measures.size();++m) {
        const double from=measures[m].time;
        const bool last=m+1==measures.size();
        const double to=last?beats.back().time+1:measures[m+1].time;
        const auto first=std::lower_bound(beats.begin(),beats.end(),from-0.0001,[](const ChartLine& p,double t){return p.time<t;});
        const auto end=last?beats.end():std::lower_bound(first,beats.end(),to-0.0001,[](const ChartLine& p,double t){return p.time<t;});
        const auto count=static_cast<std::size_t>(end-first);
        if(!count || std::abs(first->time-from)>.002) throw std::runtime_error("Invalid musical line map");
        const double number=measures[m].index;
        const double span=last?1.:measures[m+1].index-number;
        for(std::size_t j=0;j<count;++j) anchors.push_back({j?first[j].time:from,number+span*j/count});
        if(last) {
            // The native beat table includes every beat of the final measure,
            // even if the audio ends partway through it. Extrapolate its last
            // beat using the BPM clock, rather than stretching a partial bar.
            double step=1.;
            if(beats.size()>1) step=clock.at(beats.back().time)-clock.at(beats[beats.size()-2].time);
            if(step<=0 || !std::isfinite(step)) throw std::runtime_error("Invalid musical tempo map");
            anchors.push_back({clock.time_at(clock.at(beats.back().time)+step),number+1.});
        }
    }
    MusicalTimeline result;
    for(std::size_t i=0;i+1<anchors.size();++i) {
        const auto a=anchors[i],b=anchors[i+1];
        if(b.time<=a.time || b.measure<=a.measure) throw std::runtime_error("Invalid musical line map");
        result.points_.push_back(a);
        const double begin=clock.at(a.time),span=clock.at(b.time)-begin;
        if(span<=0) throw std::runtime_error("Invalid musical tempo map");
        // Preserve BPM changes inside a beat. Ignore lane-scroll multipliers;
        // each native beat endpoint remains the authoritative musical anchor.
        auto t=std::upper_bound(clock.points.begin(),clock.points.end(),a.time,[](double time,const Clock::Point& p){return time<p.time;});
        for(;t!=clock.points.end() && t->time<b.time;++t)
            result.points_.push_back({t->time,a.measure+(b.measure-a.measure)*(clock.at(t->time)-begin)/span});
    }
    result.points_.push_back(anchors.back());
    return result;
}
double MusicalTimeline::position(double seconds) const {
    if(empty() || !std::isfinite(seconds)) return 0;
    auto it=std::upper_bound(points_.begin(),points_.end(),seconds,[](double t,const Point& p){return t<p.time;});
    if(it==points_.begin()) ++it;
    if(it==points_.end()) --it;
    const auto& a=*(it-1); const auto& b=*it;
    return a.measure+(seconds-a.time)/(b.time-a.time)*(b.measure-a.measure);
}
double MusicalTimeline::seconds(double measure) const {
    if(empty() || !std::isfinite(measure)) return 0;
    auto it=std::upper_bound(points_.begin(),points_.end(),measure,[](double m,const Point& p){return m<p.measure;});
    if(it==points_.begin()) ++it;
    if(it==points_.end()) --it;
    const auto& a=*(it-1); const auto& b=*it;
    const double span=b.measure-a.measure;
    return span>0?a.time+(measure-a.measure)/span*(b.time-a.time):b.time;
}
}
