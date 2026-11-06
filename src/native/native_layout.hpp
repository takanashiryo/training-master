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
inline constexpr WorldLayout world_1209{616,600,584,633,656,1072,1040,1200,1568,1576,1580,1588};
struct ResultLayout {
    std::size_t stride{1728}, tag{1716}, excluded{1726}, challenge_present{1720};
    // Zero means the native getter exists. EG and initial NABLA inline a limit of 3.
    unsigned fixed_timed_limit{};
    std::size_t tail_shift{};
    std::size_t options_size{236}, difficulty{244}, hash_base{808}, gauge_base{872}, gauge_stride{276};
    // Translate the fields used by practice records. EG has smaller options,
    // prefix and gauge entries; these regions do not share one tail delta.
    std::size_t field(std::size_t offset) const {
        if(offset>=1700) return offset-tail_shift;
        if(offset>=872) return gauge_base+(offset-872)/276*gauge_stride+(offset-872)%276;
        if(offset>=808) return hash_base+offset-808;
        if(offset>=244) return difficulty+offset-244;
        return offset;
    }
    bool operator==(const ResultLayout&) const = default;
};
inline constexpr ResultLayout result_1226{1720,1708,1718,1712,3,8,236,244,800,864};
inline constexpr ResultLayout result_1209{1416,1400,1410,1404,3,316,160,168,720,784,200};
inline constexpr ResultLayout result_early{1728,1716,1726,1720,3};
}
