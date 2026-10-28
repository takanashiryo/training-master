#include "core/chart.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace training {
namespace {
constexpr std::size_t max_objects=100000, max_points=1000000;
template<class T> T value(const std::byte* p,std::size_t offset) {
    T result; std::memcpy(&result,p+offset,sizeof(T)); return result;
}
std::vector<std::byte> array(std::uintptr_t header,std::size_t stride,std::size_t limit,ChartReader read) {
    std::uintptr_t bounds[3]{};
    if (!read(header,bounds,sizeof(bounds))) throw std::runtime_error("Cannot read chart array");
    const auto begin=bounds[0],end=bounds[1],capacity=bounds[2];
    if (end<begin || capacity<end || (!begin && (end || capacity)) ||
        (end-begin)%stride || (end-begin)/stride>limit)
        throw std::runtime_error("Invalid chart array bounds");
    std::vector<std::byte> result(end-begin);
    if (!result.empty() && !read(begin,result.data(),result.size())) throw std::runtime_error("Cannot copy chart objects");
    return result;
}
bool time_valid(float t) { return std::isfinite(t) && t>=-60.f && t<=86400.f; }
}
std::shared_ptr<const Chart> read_native_chart(std::uintptr_t data,ChartReader read) {
    auto chart=std::make_shared<Chart>();
    try {
        if (!data || !read) throw std::runtime_error("Native chart is unavailable");
        // FumenDataFromVoxFile: chips +8 (48 bytes), holds +32 (72),
        // lasers +56 (88), BPM segments +80 (20), measure/beat lines +104/+128
        // (8 bytes: seconds + original zero-based index). No native pointers escape.
        for (const auto stride:{48u,72u}) {
            const auto objects=array(data+(stride==48?8:32),stride,max_objects,read);
            for (std::size_t i=0;i<objects.size();i+=stride) {
                const auto p=objects.data()+i;
                const auto start=value<float>(p,4);
                const auto end=stride==48?start:value<float>(p,8);
                const auto lane=value<unsigned>(p,stride==48?8:12);
                if (!time_valid(start) || !time_valid(end) || end<start || lane<1 || lane>6)
                    throw std::runtime_error("Invalid note time or lane");
                chart->notes.push_back({start,end,lane-1});
            }
        }
        const auto lasers=array(data+56,88,max_objects,read);
        std::size_t total_points{};
        for (std::size_t i=0;i<lasers.size();i+=88) {
            const auto p=lasers.data()+i;
            ChartLaser laser;
            const auto side=value<unsigned>(p,4);
            laser.width=value<float>(p,8);
            if (side<1 || side>2 || !std::isfinite(laser.width) || laser.width<=0 || laser.width>16)
                throw std::runtime_error("Invalid laser side or width");
            laser.side=side-1;
            // Nested control-point vector belongs to the native laser record.
            // Read its copied header, then copy the point values using Reader.
            std::uintptr_t bounds[3]; std::memcpy(bounds,p+16,sizeof(bounds));
            const auto begin=bounds[0],end=bounds[1],capacity=bounds[2];
            if (end<begin || capacity<end || (!begin && (end || capacity)) ||
                (end-begin)%48 || (end-begin)/48>max_points-total_points)
                throw std::runtime_error("Invalid laser point bounds");
            std::vector<std::byte> points(end-begin);
            if (!points.empty() && !read(begin,points.data(),points.size())) throw std::runtime_error("Cannot copy laser points");
            total_points+=points.size()/48;
            for (std::size_t j=0;j<points.size();j+=48) {
                const auto q=points.data()+j;
                ChartPoint point{value<float>(q,0),value<float>(q,4),value<unsigned>(q,8),value<std::uint8_t>(q,16)!=0};
                if (!time_valid(point.time) || !std::isfinite(point.position) || std::abs(point.position)>16 ||
                    (!laser.points.empty() && point.time<laser.points.back().time))
                    throw std::runtime_error("Invalid laser control point");
                laser.points.push_back(point);
            }
            if (!laser.points.empty()) chart->lasers.push_back(std::move(laser));
        }
        for(const auto offset:{104u,128u}) {
            const auto lines=array(data+offset,8,max_objects,read);
            auto& target=offset==104?chart->measures:chart->beats;
            for(std::size_t i=0;i<lines.size();i+=8) {
                const auto t=value<float>(lines.data()+i,0);
                if(!time_valid(t)) throw std::runtime_error("Invalid measure time");
                target.push_back({t,value<unsigned>(lines.data()+i,4)});
            }
        }
        const auto tempos=array(data+80,20,max_objects,read);
        for(std::size_t i=0;i<tempos.size();i+=20)
            chart->tempos.push_back({value<float>(tempos.data()+i,0),value<float>(tempos.data()+i,4)});
        chart->timing=MusicalTimeline::build(chart->measures,chart->beats,chart->tempos);
        std::stable_sort(chart->notes.begin(),chart->notes.end(),[](const auto& a,const auto& b){return a.start<b.start;});
        for (const auto& note:chart->notes) {
            chart->first_time=std::min(chart->first_time,note.start);
            chart->last_time=std::max(chart->last_time,note.end);
        }
        for (const auto& laser:chart->lasers) {
            chart->first_time=std::min(chart->first_time,laser.points.front().time);
            chart->last_time=std::max(chart->last_time,laser.points.back().time);
        }
    } catch (const std::exception& e) {
        chart->notes.clear(); chart->lasers.clear(); chart->measures.clear(); chart->beats.clear(); chart->tempos.clear(); chart->timing={};
        chart->error=e.what();
    }
    return chart;
}
}
