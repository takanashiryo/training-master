#pragma once
#include "native/native_layout.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace training::native {
enum class Symbol {
#define TM_SYMBOL(name, kind) name,
#include "native/profile_symbols.inc"
#undef TM_SYMBOL
    Count, None=Count
};
struct Addresses {
#define TM_SYMBOL(name, kind) std::uintptr_t name{};
#include "native/profile_symbols.inc"
#undef TM_SYMBOL
};
enum class ReferenceKind { Relative32, ImageRelative32 };
enum class Access { Code, ReadOnly, Writable, Internal, ImageBase };
struct Reference {
    std::size_t operand{}, next{};
    Symbol target{Symbol::None};
    // Internal references target this byte offset in the same function.
    std::size_t internal{};
    ReferenceKind kind{ReferenceKind::Relative32};
    Access access{Access::ReadOnly};
    unsigned identity{};
};
struct PatchVariant {
    std::size_t offset;
    // Exact, equal-length alternatives reviewed for this ABI family.
    // They affect matching only; the resolver never patches game memory.
    std::string_view original,replacement,name;
};
// Reviewed content-selector range checks, not structure/member operands.
// All three values are 32-bit immediates in otherwise exact instructions.
struct SelectorBounds { std::size_t maximum,exclusive,fallback; };
struct Anchor {
    const char* name;
    Symbol symbol;
    // Entire native function; only address operands are wildcarded.
    // Member offsets, record sizes, mode IDs and branches remain checked.
    const char* pattern;
    std::span<const Reference> references;
    std::span<const PatchVariant> patches{};
    std::span<const SelectorBounds> selectors{};
};
struct Family {
    const char* name;
    std::span<const Anchor> anchors;
    WorldLayout world{};
    ResultLayout result{};
    bool chart_gauge_gain{true};
    // EG owns a typed result pointer; NABLA exposes an RTTI-casting getter.
    bool direct_stage_data{};
};
struct Resolution {
    Addresses addresses;
    CameraDefaults camera;
    WorldLayout world{};
    ResultLayout result{};
    bool chart_gauge_gain{true};
    std::string family;
    unsigned interface_methods{};
    std::vector<std::string> details;
};
// Bounded inspection of a mapped PE image; no code execution or hooks.
bool resolve_image(std::span<const std::byte> image,std::uintptr_t base,
                   const Family& family,Resolution& result,std::string& reason);
// Runtime entry: builds shared RTTI evidence once, then evaluates all families.
bool resolve_image(std::span<const std::byte> image,std::uintptr_t base,
                   Resolution& result,std::string& reason);
std::span<const Family> families();
std::string_view symbol_name(Symbol symbol);
}
