#pragma once
#include <array>
namespace training {
enum class GaugeType { Effective, Excessive, Maxxive, Permissive, ExcessiveArs, MaxxiveArs, Count };
inline constexpr std::array<const char*,6> gauge_names{
    "EFFECTIVE", "EXCESSIVE", "MAXXIVE", "PERMISSIVE", "EXCESSIVE (ARS)", "MAXXIVE (ARS)"};
struct GaugeConfig {
    bool enabled{};
    GaugeType type{GaugeType::Effective};
    float initial{100.f};
};
}
