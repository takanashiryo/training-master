#pragma once
#include <cstddef>
#include <array>
#include <cmath>
#include <cstdint>

namespace training::native {
// Values read from verified native constructors and their referenced data.
// This is independent of any song's initial camera events.
struct CameraDefaults {
    float angle{};
    std::array<std::int32_t,2> center{};
    std::array<float,3> position{},rotation{};
    bool verified{};
    explicit operator bool() const {
        if(!verified || !std::isfinite(angle) || std::fabs(angle)>=1e6f) return false;
        for(auto value:center) if(value<=-1000000 || value>=1000000) return false;
        for(const auto& vector:{position,rotation})
            for(float value:vector) if(!std::isfinite(value) || std::fabs(value)>=1e6f) return false;
        return true;
    }
};
// Published with the verified function graph. Offsets are never selected from
// a filename, build date or SHA. Unchanged object prefixes stay in their owner.
struct WorldLayout {
    std::size_t renderer{584}, input{568}, converter{552};
    std::size_t forced_end{601}, music{624}, chart_path{1040}, audio_path{1008};
    std::size_t gauge_types{1184};
    std::size_t demo{1536}, special{1544}, no_fail{1550}, battle{1560};
    bool operator==(const WorldLayout&) const = default;
};
inline constexpr WorldLayout world_1013{};
inline constexpr WorldLayout world_0901{584,568,552,601,624,1040,1008,1184,1536,1544,1556,1568};
inline constexpr WorldLayout world_0805{616,600,584,633,656,1072,1040,1216,1568,1576,1588,1600};
inline constexpr WorldLayout world_1226{616,600,584,633,656,1072,1040,1200,1552,1560,1572,1584};
struct ResultLayout {
    std::size_t stride{1728}, tag{1716}, excluded{1726}, challenge_present{1720};
    // Zero means the native getter exists. Initial NABLA inlines a limit of 3.
    unsigned fixed_timed_limit{};
    std::size_t tail_shift{};
    // The field added at +800 in January shifts the hash container and later
    // result fields. The options, difficulty and clear fields stay unchanged.
    std::size_t field(std::size_t current_offset) const { return current_offset>=808?current_offset-tail_shift:current_offset; }
    bool operator==(const ResultLayout&) const = default;
};
inline constexpr ResultLayout result_1226{1720,1708,1718,1712,3,8};
inline constexpr ResultLayout result_early{1728,1716,1726,1720,3};
}
