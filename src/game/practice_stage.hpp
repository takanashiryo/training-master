#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace training {
using NativeSerialize=std::int64_t(__fastcall*)(std::uintptr_t,std::uintptr_t);
using NativeContinue=bool(__fastcall*)(std::uintptr_t);
using NativeFlow=unsigned(__fastcall*)(std::uintptr_t,int,std::uintptr_t);
inline constexpr std::size_t stage_record_size=1728;
std::size_t stage_stride();
// A private tag in the selected ABI's absent optional challenge-id field, preserved by native
// record copies. This is local bookkeeping, never a score to serialize.
inline constexpr std::uint32_t practice_stage_tag=0x544d5052;
class PracticeStageRecord {
public:
    PracticeStageRecord(unsigned ordinal,unsigned music,unsigned difficulty,std::span<const std::byte,236> options);
    PracticeStageRecord(const PracticeStageRecord&)=delete;
    PracticeStageRecord& operator=(const PracticeStageRecord&)=delete;
    std::uintptr_t address() const { return reinterpret_cast<std::uintptr_t>(bytes_.data()); }
private:
    alignas(16) std::array<std::byte,stage_record_size> bytes_{};
    std::array<std::uintptr_t,11> sentinel_{};
    std::array<std::uintptr_t,16> buckets_{};
};
bool is_practice_stage(std::uintptr_t record);
bool append_practice_stage(std::uintptr_t image,std::uintptr_t data,const PracticeStageRecord& record);
std::int64_t serialize_practice_safe(std::uintptr_t image,std::uintptr_t data,std::uintptr_t xml,NativeSerialize original);
bool can_continue_practice_safe(std::uintptr_t image,std::uintptr_t data,NativeContinue original);
unsigned decide_practice_flow(std::uintptr_t image,std::uintptr_t flow,int state,std::uintptr_t argument,
                             NativeContinue continuation,NativeFlow original);
}
