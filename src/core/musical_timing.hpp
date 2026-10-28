#pragma once
#include <span>
#include <vector>

namespace training {
struct ChartLine { float time{}; unsigned index{}; };
struct ChartTempo { float time{}, bpm{}; };
// The preview axis is measure number + fractional beat position. Song commands
// remain in seconds; this owned mapping is used in both drawing and picking.
class MusicalTimeline {
public:
    struct Point { double time{}, measure{}; };
    static MusicalTimeline build(std::span<const ChartLine> measures,std::span<const ChartLine> beats,
                                 std::span<const ChartTempo> tempos);
    bool empty() const { return points_.size()<2; }
    double position(double seconds) const;
    double seconds(double measure) const;
    const std::vector<Point>& points() const { return points_; }
private:
    std::vector<Point> points_;
};
}
