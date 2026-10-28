#include "game/autoplay_resolver.hpp"
#include <hde/hde64.h>
#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace training::autoplay_discovery {
namespace {
struct Region { std::uint32_t rva, size; bool code; };
struct Function { std::uint32_t begin, end; };

bool readable(const void* address, std::size_t length) {
    auto current = reinterpret_cast<std::uintptr_t>(address);
    if (!current || length > UINTPTR_MAX - current) return false;
    const auto end = current + length;
    while (current < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(current), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                              PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
        const auto next = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= current) return false;
        current = next;
    }
    return true;
}

struct Image {
    const unsigned char* base;
    std::uint32_t size{};
    std::vector<Region> regions;
    std::vector<Function> functions;

    explicit Image(HMODULE module) : base(reinterpret_cast<const unsigned char*>(module)) {}
    template<class T> T read(std::uint32_t rva) const {
        T value{}; std::memcpy(&value, base + rva, sizeof(value)); return value;
    }
    bool inside(std::uint32_t rva, std::size_t count) const {
        return rva < size && count <= size - rva;
    }
    bool in_region(std::uint32_t rva, std::size_t count, bool code) const {
        for (auto r : regions)
            if (r.code == code && rva >= r.rva && rva - r.rva < r.size && count <= r.size - (rva - r.rva)) return true;
        return false;
    }
    std::uint32_t rva_of(std::uintptr_t va) const {
        const auto start = reinterpret_cast<std::uintptr_t>(base);
        return va >= start && va - start < size ? static_cast<std::uint32_t>(va - start) : UINT32_MAX;
    }
    bool initialize() {
        if (!readable(base, sizeof(IMAGE_DOS_HEADER))) return false;
        const auto dos = read<IMAGE_DOS_HEADER>(0);
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof(IMAGE_DOS_HEADER) || dos.e_lfanew > 0x100000) return false;
        if (!readable(base + dos.e_lfanew, sizeof(IMAGE_NT_HEADERS64))) return false;
        const auto nt = read<IMAGE_NT_HEADERS64>(dos.e_lfanew);
        if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
            nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION ||
            !nt.OptionalHeader.SizeOfImage || nt.OptionalHeader.SizeOfImage > 0x40000000 ||
            !nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections > 96) return false;
        size = nt.OptionalHeader.SizeOfImage;
        const auto table = static_cast<std::uint32_t>(dos.e_lfanew) + 24 + nt.FileHeader.SizeOfOptionalHeader;
        const auto table_size = nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
        if (!inside(table, table_size) || !readable(base + table, table_size)) return false;
        for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            const auto section = read<IMAGE_SECTION_HEADER>(table + i * sizeof(IMAGE_SECTION_HEADER));
            const auto length = section.Misc.VirtualSize;
            if (!length || !(section.Characteristics & IMAGE_SCN_MEM_READ)) continue;
            if (!inside(section.VirtualAddress, length) || !readable(base + section.VirtualAddress, length)) return false;
            for (auto r : regions)
                if (section.VirtualAddress < r.rva + r.size && r.rva < section.VirtualAddress + length) return false;
            regions.push_back({section.VirtualAddress, length, (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0});
        }
        const auto exception = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (!exception.Size || exception.Size % sizeof(RUNTIME_FUNCTION) ||
            !in_region(exception.VirtualAddress, exception.Size, false)) return false;
        for (std::uint32_t offset = 0; offset < exception.Size; offset += sizeof(RUNTIME_FUNCTION)) {
            const auto f = read<RUNTIME_FUNCTION>(exception.VirtualAddress + offset);
            if (f.BeginAddress >= f.EndAddress || !in_region(f.BeginAddress, f.EndAddress - f.BeginAddress, true)) return false;
            functions.push_back({f.BeginAddress, f.EndAddress});
        }
        std::sort(functions.begin(), functions.end(), [](auto a, auto b) { return a.begin < b.begin; });
        return true;
    }

    // MSVC x64 TypeDescriptor -> complete-object locator -> primary vtable.
    std::uint32_t vtable(const char* name) const {
        std::vector<std::uint32_t> descriptors, locators, tables;
        const auto length = std::strlen(name) + 1;
        for (auto r : regions) if (!r.code && r.size >= length + 16) {
            for (std::uint32_t i = 16; i <= r.size - length; ++i)
                if (base[r.rva + i] == '.' && !std::memcmp(base + r.rva + i, name, length))
                    descriptors.push_back(r.rva + i - 16);
        }
        if (descriptors.size() != 1) return 0;
        for (auto r : regions) if (!r.code && r.size >= 24) {
            for (std::uint32_t i = 0; i <= r.size - 24; i += 4) {
                const auto at = r.rva + i;
                if (read<std::uint32_t>(at) == 1 && read<std::uint32_t>(at + 4) == 0 &&
                    read<std::uint32_t>(at + 8) == 0 && read<std::uint32_t>(at + 12) == descriptors[0] &&
                    in_region(read<std::uint32_t>(at + 16), 16, false) && read<std::uint32_t>(at + 20) == at)
                    locators.push_back(at);
            }
        }
        for (auto locator : locators) for (auto r : regions) if (!r.code && r.size >= 16) {
            for (std::uint32_t i = 0; i <= r.size - 16; i += 8) {
                const auto at = r.rva + i;
                if (read<std::uintptr_t>(at) == reinterpret_cast<std::uintptr_t>(base + locator) &&
                    in_region(rva_of(read<std::uintptr_t>(at + 8)), 1, true)) tables.push_back(at + 8);
            }
        }
        return tables.size() == 1 ? tables[0] : 0;
    }
    std::vector<std::uint32_t> methods(std::uint32_t table) const {
        std::vector<std::uint32_t> result;
        for (unsigned i = 0; i < 64 && in_region(table + i * 8, 8, false); ++i) {
            const auto target = rva_of(read<std::uintptr_t>(table + i * 8));
            if (!in_region(target, 1, true)) break;
            if (std::find(result.begin(), result.end(), target) == result.end()) result.push_back(target);
        }
        return result;
    }
    Function function(std::uint32_t entry) const {
        const auto it = std::lower_bound(functions.begin(), functions.end(), entry,
                                         [](auto f, auto value) { return f.begin < value; });
        return it != functions.end() && it->begin == entry ? *it : Function{};
    }
};

using Pattern = std::initializer_list<int>;
std::vector<std::uint32_t> matches(const Image& image, Function f, Pattern pattern) {
    std::vector<std::uint32_t> result;
    if (!f.end || f.end - f.begin > 0x20000) return result;
    for (auto at = f.begin; at < f.end;) {
        if (pattern.size() <= f.end - at) {
            bool same = true; unsigned offset = 0;
            for (int b : pattern) { if (b >= 0 && image.base[at + offset] != b) same = false; ++offset; }
            if (same) result.push_back(at);
        }
        // HDE may read up to 15 instruction bytes; never cross a mapped code section.
        unsigned char bytes[16]{};
        const auto count = (std::min)(std::uint32_t{15}, f.end - at);
        std::memcpy(bytes, image.base + at, count);
        hde64s instruction{};
        hde64_disasm(bytes, &instruction);
        if (!instruction.len || instruction.len > count || (instruction.flags & F_ERROR)) break;
        at += instruction.len;
    }
    return result;
}

bool clean_entry(const Image& image, Function f) {
    if (!f.end || f.end - f.begin < 16) return false;
    // Accept stack/register setup, never an existing jump, call, trap or return trampoline.
    const auto at = f.begin;
    const auto prefix = image.base[at];
    const auto opcode = image.base[at + 1];
    const bool setup = (prefix >= 0x50 && prefix <= 0x57) ||
        (prefix >= 0x40 && prefix <= 0x4F && (opcode == 0x89 || opcode == 0x8B || opcode == 0x8D ||
        opcode == 0x81 || opcode == 0x83 || (opcode >= 0x50 && opcode <= 0x57)));
    if (!setup) return false;
    for (auto p = at; p - at < 16;) {
        unsigned char bytes[16]{};
        const auto available = (std::min)(std::uint32_t{15}, f.end - p);
        std::memcpy(bytes,image.base+p,available);
        hde64s ins{}; hde64_disasm(bytes,&ins);
        if (!ins.len || ins.len > available || (ins.flags & F_ERROR) || ins.opcode == 0xE8 || ins.opcode == 0xE9 ||
            ins.opcode == 0xEB || ins.opcode == 0xC2 || ins.opcode == 0xC3 || ins.opcode == 0xCC ||
            (ins.opcode >= 0x70 && ins.opcode <= 0x7F) ||
            (ins.opcode == 0x0F && ins.opcode2 >= 0x80 && ins.opcode2 <= 0x8F) ||
            (ins.opcode == 0xFF && ins.modrm_reg >= 2 && ins.modrm_reg <= 5)) return false;
        p += ins.len;
    }
    return true;
}
bool field(std::uint32_t value) { return value >= 8 && value < 0x4000 && value % 8 == 0; }

Result resolve_impl(HMODULE module) {
    Image image(module);
    if (!image.initialize()) return {{}, "Invalid or unreadable x64 PE image/exception directory"};
    const auto world = image.vtable(".?AVGameWorldKFC@Game@@");
    const auto referee = image.vtable(".?AVNoteReferee@Game@@");
    const auto context = image.vtable(".?AVGameContext@Game@@");
    const auto timer = image.vtable(".?AVGameTimer@Game@@");
    if (!world || !referee || !context || !timer) return {{}, "Required RTTI/primary vtable missing or ambiguous"};
    Layout layout{};
    layout.world_vtable = reinterpret_cast<std::uintptr_t>(image.base + world);
    layout.context_vtable = reinterpret_cast<std::uintptr_t>(image.base + context);
    layout.timer_vtable = reinterpret_cast<std::uintptr_t>(image.base + timer);
    unsigned updates = 0, judges = 0, loads = 0, clocks = 0;
    for (auto method : image.methods(world)) {
        const auto f = image.function(method);
        // Timer update dispatch; displacement is an extracted field, not a version constant.
        const auto update = matches(image, f, {0x48,0x8B,0x89,-1,-1,-1,-1,0x48,0x85,0xC9,0x74,-1,0x48,0x8B,0x01,0xFF,0x50,0x38});
        if (update.size() == 1 && update[0] - method < 96 && clean_entry(image, f) &&
            !matches(image,f,{0x48,0xB8,0xDB,0x34,0xB6,0xD7,0x82,0xDE,0x1B,0x43}).empty()) {
            ++updates; layout.update = reinterpret_cast<std::uintptr_t>(image.base + method);
            layout.timer = image.read<std::uint32_t>(update[0] + 3);
        }
        // MainLoad: world->context assignment plus the demo switch setting shared flags[0]=1.
        const auto ctx = matches(image,f,{0x4D,0x8D,0xA5,-1,-1,-1,-1,0x48,0x8B,0xD0,0x49,0x8B,0xCC,0xE8,-1,-1,-1,-1});
        const auto flags = matches(image,f,{0x49,0x8B,0x14,0x24,0x48,0x83,0xC2,-1,0x48,0x8D,0x4D,-1,0xE8,-1,-1,-1,-1,0xBE,0x80,0,0,0,0x48,0x8B,0x45,-1,0xC6,0,1});
        if (ctx.size() == 1 && flags.size() == 1 && ctx[0] < flags[0] && flags[0] - ctx[0] < 512 &&
            !matches(image,{f.begin,(std::min)(f.end,f.begin+128)},{0x4C,0x8B,0xE9}).empty()) {
            ++loads; layout.context = image.read<std::uint32_t>(ctx[0] + 3);
            layout.flags = image.base[flags[0] + 7];
        }
    }
    if (updates != 1 || loads != 1) return {{}, "GameWorld update/MainLoad structure missing, modified or ambiguous"};
    if (!field(layout.context) || !field(layout.flags) || layout.flags >= 128 || !field(layout.timer) || layout.context == layout.timer)
        return {{}, "Invalid extracted GameWorld/GameFlags fields"};
    for (auto method : image.methods(referee)) {
        const auto f = image.function(method);
        if (!clean_entry(image,f)) continue;
        // Result IDs, 40-byte cache division and fourth-argument float handling together identify AutoJudge.
        if (matches(image,f,{0xC7,0x03,6,0,0,0}).size() == 1 && matches(image,f,{0xC7,0x03,5,0,0,0}).size() == 1 &&
            !matches(image,f,{0x48,0xB8,0x67,0x66,0x66,0x66,0x66,0x66,0x66,0x66}).empty() &&
            !matches(image,{f.begin,(std::min)(f.end,f.begin+128)},{0x0F,0x28,0xF3}).empty()) {
            ++judges; layout.judge = reinterpret_cast<std::uintptr_t>(image.base + method);
        }
    }
    if (judges != 1) return {{}, "AutoJudge structure missing, modified or ambiguous"};
    for (auto method : image.methods(timer)) {
        if (!image.in_region(method,26,true)) continue;
        // Leaf clock accessor may have no .pdata entry. Verify its complete branch/return shape.
        const auto clock = matches(image,{method,method+26},{0x80,0x79,-1,0,0x74,0x0B,0xF3,0x0F,0x10,0x41,-1,
            0xF3,0x0F,0x58,0x41,-1,0xC3,0x0F,0x57,0xC0,0xF3,0x0F,0x58,0x41,-1,0xC3});
        if (clock.size() == 1 && clock[0] == method && image.base[method+15] == image.base[method+24]) {
            ++clocks; layout.time_enabled = image.base[method+2]; layout.time_offset = image.base[method+10];
            layout.time_base = image.base[method+15];
        }
    }
    if (clocks != 1 || layout.time_base < 8 || layout.time_offset < 8 || layout.time_enabled < 8 ||
        layout.time_base % 4 || layout.time_offset % 4 || layout.time_base >= 128 || layout.time_offset >= 128 ||
        layout.time_enabled >= 128 || layout.time_base == layout.time_offset ||
        (layout.time_enabled >= layout.time_base && layout.time_enabled < layout.time_base+4) ||
        (layout.time_enabled >= layout.time_offset && layout.time_enabled < layout.time_offset+4))
        return {{}, "GameTimer accessor/layout missing or ambiguous"};
    return {layout, nullptr};
}

Result resolve_cpp(HMODULE module) {
    try { return resolve_impl(module); }
    catch (...) { return {{}, "Discovery could not allocate or process image metadata"}; }
}
}

Result resolve(HMODULE module) {
    // Keep SEH in a separate frame from C++ containers and their destructors.
    __try { return resolve_cpp(module); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return {{}, "Memory changed or became unreadable during discovery"}; }
}
}
