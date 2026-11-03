#include "game/autoplay_resolver.hpp"
#include <hde/hde64.h>
#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <vector>
#include <string_view>

namespace training::autoplay_discovery {
namespace {
#include "game/judgment_patterns.inc"
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
    std::uint32_t vtable(const char* name,std::uint32_t subobject_offset=0) const {
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
                if (read<std::uint32_t>(at) == 1 && read<std::uint32_t>(at + 4) == subobject_offset &&
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

bool clean_entry(const Image& image, Function f,unsigned prefix_length=16) {
    if (!f.end || f.end - f.begin < 16) return false;
    // Accept stack/register setup, never an existing jump, call, trap or return trampoline.
    const auto at = f.begin;
    const auto prefix = image.base[at];
    const auto opcode = image.base[at + 1];
    const bool setup = (prefix >= 0x50 && prefix <= 0x57) ||
        (prefix >= 0x40 && prefix <= 0x4F && (opcode == 0x89 || opcode == 0x8B || opcode == 0x8D ||
        opcode == 0x81 || opcode == 0x83 || (opcode >= 0x50 && opcode <= 0x57)));
    if (!setup) return false;
    for (auto p = at; p - at < prefix_length;) {
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
    // GameWorld's separate draw method dispatches both component lists via
    // virtual slot 2. DrawEvent listeners (including VOL cursors) run here,
    // after the update hook has left its thread-local world scope.
    if (image.in_region(world+32,8,false)) {
        const auto draw=image.rva_of(image.read<std::uintptr_t>(world+32));
        const auto f=image.function(draw);
        const auto dispatch=matches(image,f,{0x48,0x8b,0x0b,0x48,0x8b,0x01,0xff,0x50,0x10,0x48,0x83,0xc3,0x10});
        if(clean_entry(image,f) && dispatch.size()==2)
            layout.draw=reinterpret_cast<std::uintptr_t>(image.base+draw);
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
    // IsJudged runs before each note's manual/automatic dispatch AND follow
    // update. Identify its note-ID lookup, FNV hash and 40-byte result walk.
    unsigned gates=0;
    for(auto method:image.methods(referee)) {
        const auto f=image.function(method);
        // The first legitimate virtual call starts at byte 15 in this leaf.
        if(!clean_entry(image,f,15)) continue;
        if(matches(image,f,{0x48,0x8B,0x0A,0x48,0x8B,0x01,0xFF,0x50,0x38}).size()==1 &&
           matches(image,f,{0x48,0xBA,0xB3,1,0,0,0,1,0,0}).size()==1 &&
           matches(image,f,{0x8B,0x01,0xFF,0xC8,0x83,0xF8,9}).size()==1 &&
           matches(image,f,{0x48,0x83,0xC1,0x28}).size()==1) {
            ++gates; layout.note_gate=reinterpret_cast<std::uintptr_t>(image.base+method);
        }
    }
    if(gates!=1) layout.note_gate=0;
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
    // Optional score callback. Full-body signatures preserve every layout and
    // branch byte; only six/eight audited rel32 operands vary across builds.
    // Failure affects the counter only, not existing AutoPlay or training.
    auto matches_guard=[&](std::uint32_t at,std::string_view guard) {
        if(!image.in_region(at,guard.size()/2,true)) return false;
        auto nibble=[](char c)->unsigned { return c<='9'?static_cast<unsigned>(c-'0'):static_cast<unsigned>(c-'a'+10); };
        for(std::size_t i=0;i<guard.size()/2;++i) {
            if(guard[2*i]=='?' && guard[2*i+1]=='?') continue;
            if(image.base[at+i]!=(nibble(guard[2*i])<<4|nibble(guard[2*i+1]))) return false;
        }
        return true;
    };
    // Song presentation uses separate BM2D roots and an engine render callback.
    // Optional discovery: failure disables this control without disabling training.
    auto& visuals=layout.song_visuals;
    auto table_address=[&](const char* type) -> std::uintptr_t {
        const auto table=image.vtable(type);
        return table?reinterpret_cast<std::uintptr_t>(image.base+table):0;
    };
    visuals.ex_bg=table_address(".?AVGameExBgView@Game@@");
    visuals.autopara=table_address(".?AVGameAutoparaBgView@Game@@");
    visuals.post=table_address(".?AVGamePostEffectView@Game@@");
    visuals.layer=table_address(".?AVCLayer@BM2D@@");
    visuals.priority=table_address(".?AVPriorityRangePostEffector@Game@@");
    visuals.target=table_address(".?AVCPriorityRangeRenderTarget@@");
    visuals.live2d=table_address(".?AVCLive2DCrewComponent@@");
    if(visuals.live2d) {
        const auto table=image.rva_of(visuals.live2d);
        if(image.in_region(table,72,false)) {
            const auto setter=image.rva_of(image.read<std::uintptr_t>(table+64));
            // Component visibility +44 and LAppModel visibility +573, through
            // 64-byte entries. Verify every instruction; no external calls.
            if(matches_guard(setter,"88512c4c8b4918488b4110493bc1741c488b50084883c0404c8b42080fb6512c4188903d020000493bc175e4c3"))
                visuals.set_live2d_visible=reinterpret_cast<song_visuals::SetVisible>(reinterpret_cast<std::uintptr_t>(image.base+setter));
        }
    }
    if(visuals.layer) {
        const auto table=image.rva_of(visuals.layer);
        if(image.in_region(table,384,false)) {
            const auto setter=image.rva_of(image.read<std::uintptr_t>(table+40));
            const auto getter=image.rva_of(image.read<std::uintptr_t>(table+376));
            if(matches_guard(setter,"8b4908440fb6c2ba0100000048ff25????????") &&
               matches_guard(getter,"4c8bdc4883ec688b4908498d53b833c0")) {
                visuals.set_visible=reinterpret_cast<song_visuals::SetVisible>(reinterpret_cast<std::uintptr_t>(image.base+setter));
                visuals.visible=reinterpret_cast<song_visuals::Visible>(reinterpret_cast<std::uintptr_t>(image.base+getter));
            }
        }
    }
    if(visuals.ex_bg) {
        const auto table=image.rva_of(visuals.ex_bg);
        if(image.in_region(table,24,false)) {
            const auto update=image.function(image.rva_of(image.read<std::uintptr_t>(table+8)));
            const auto draw=image.rva_of(image.read<std::uintptr_t>(table+16));
            if(matches(image,update,{0x4d,0x8b,0xac,0x24,0x18,2,0,0}).size()==1 &&
               matches(image,update,{0x49,0x81,0xc5,0xc0,1,0,0}).size()==1 &&
               matches_guard(draw,"4883ec2880b9300200000074??48895c2430488b997002000048897c2420488bb978020000"))
                visuals.ex_draw=reinterpret_cast<std::uintptr_t>(image.base+draw);
        }
    }
    unsigned renderers=0;
    for(const auto f:image.functions) {
        if(!clean_entry(image,f)) continue;
        if(matches(image,f,{0x4c,0x8b,0xb6,0xb0,0,0,0,0x4c,0x8b,0xbe,0xb8,0,0,0}).size()==1 &&
           matches(image,f,{0x48,0x8b,0x9e,0xc8,0,0,0,0x48,0x8b,0xbe,0xd0,0,0,0}).size()==1 &&
           matches(image,f,{0xb0,1,0x48,0x83,0xc4,0x40}).size()==1) {
            ++renderers; visuals.post_render=reinterpret_cast<std::uintptr_t>(image.base+f.begin);
        }
    }
    if(renderers!=1) visuals.post_render=0;
    // Native full-screen presenter used when a processor has no animated output
    // roots. Reuse it for an untransformed frame while hiding those roots.
    // Verify the complete body; only external rel32/RIP operands vary.
    static constexpr std::string_view raw_presenter=
        "4053574883ec7848c7442440feffffff488bd94883b9e800000000750932c04883c4785f5bc3"
        "33c048898424a00000004889842498000000488b0d????????488b014c8d8424a000000033d2ff9030010000"
        "488b0d????????488b01488d942498000000ff90400100004c8b8c24980000004c8b8424a0000000"
        "488b15????????488d4c2458e8????????90488d0d????????e8????????488b8be8000000488b0133d2"
        "488994249000000088942490000000889424940000004c8b842490000000488d542448ff502090488b00"
        "488b580848899c24a80000004885db740a488b03488bcbff500890c7442430ffffffffc744242880070000"
        "c7442420380400004533c0488bd3488b0d????????e8????????904885db740a488b03488bcbff501090"
        "488b5c24504885db742dbfffffffff8bc7f00fc1430883e801751c488b03488bcbff10f00fc17b0c83ff01750a"
        "488b03488bcbff500890488d4c2458e8????????90488b8c24980000004885c97407488b01ff501090"
        "488b8c24a00000004885c97407488b01ff501090b0014883c4785f5bc3";
    unsigned presenters=0;
    for(const auto f:image.functions) {
        if(f.end-f.begin!=raw_presenter.size()/2 || !clean_entry(image,f) || !matches_guard(f.begin,raw_presenter)) continue;
        ++presenters;
        visuals.present=reinterpret_cast<song_visuals::PostRender>(reinterpret_cast<std::uintptr_t>(image.base+f.begin));
    }
    if(presenters!=1) visuals.present=nullptr;
    unsigned judgment_matches=0;
    for(const auto f:image.functions) {
        const std::string_view guard=f.end-f.begin==670?judgment_new:
            f.end-f.begin==708?judgment_old:std::string_view{};
        if(guard.empty() || !clean_entry(image,f) || !matches_guard(f.begin,guard)) continue;
        const auto disp=image.read<std::int32_t>(f.begin+205);
        const auto target=static_cast<std::int64_t>(f.begin)+209+disp;
        if(target<0 || target>=image.size || !image.in_region(static_cast<std::uint32_t>(target),1,true)) continue;
        ++judgment_matches;
        layout.judgment_event=reinterpret_cast<std::uintptr_t>(image.base+f.begin);
        layout.judgment_gauge=reinterpret_cast<std::uintptr_t>(image.base+target);
    }
    if(judgment_matches!=1) layout.judgment_event=layout.judgment_gauge=0;
    // Native option wrapper: option index 79 (EARLY/LATE palette), with a
    // default of 2. Full wrapper + accessor bodies, both calls and RTTI names
    // are verified before the game thread is allowed to call it.
    static constexpr std::string_view color_wrapper=
        "40534883ec2085c9742c83f901740bb8010000004883c4205bc3bb01000000ba500000008bcb"
        "e8????????85c00f44c34883c4205bc3ba4f0000008d4ab2e8????????85c0b9020000000f44c14883c4205bc3";
    static constexpr std::string_view option_accessor=
        "48895c24084889742410574883ec304863f24c8d0d????????8bd94c8d05????????488b0d????????"
        "33ff33d2897c2420e8????????448bc38d5702488bc8e8????????4885c07510488b5c2440488b742448"
        "4883c4305fc381feff00000077048b7cb010488b5c24408bc7488b7424484883c4305fc3";
    auto rel=[&](std::uint32_t base,std::uint32_t offset)->std::int64_t {
        return static_cast<std::int64_t>(base)+offset+4+image.read<std::int32_t>(base+offset);
    };
    auto type_at=[&](std::int64_t target,std::string_view name) {
        return target>=0 && target<image.size && image.in_region(static_cast<std::uint32_t>(target),16+name.size()+1,false) &&
            std::memcmp(image.base+target+16,name.data(),name.size()+1)==0;
    };
    unsigned color_matches=0;
    for(const auto f:image.functions) {
        if(f.end-f.begin!=color_wrapper.size()/2 || !matches_guard(f.begin,color_wrapper)) continue;
        const auto target=rel(f.begin,0x27);
        if(target<0 || target>=image.size || target!=rel(f.begin,0x3f)) continue;
        const auto accessor=static_cast<std::uint32_t>(target);
        const auto func=image.function(accessor);
        if(func.end-func.begin!=option_accessor.size()/2 || !matches_guard(accessor,option_accessor) ||
           !type_at(rel(accessor,0x15),".?AVCPlayerParamGameData@@") ||
           !type_at(rel(accessor,0x1e),".?AVCBaseGameData@@")) continue;
        const auto global=rel(accessor,0x25),cast=rel(accessor,0x32),lookup=rel(accessor,0x40);
        if(global<0 || cast<0 || lookup<0 || global>=image.size || cast>=image.size || lookup>=image.size ||
           !image.in_region(static_cast<std::uint32_t>(global),sizeof(std::uintptr_t),false) ||
           !image.in_region(static_cast<std::uint32_t>(cast),1,true) ||
           !image.in_region(static_cast<std::uint32_t>(lookup),1,true)) continue;
        ++color_matches; layout.judgment_color_option=reinterpret_cast<std::uintptr_t>(image.base+f.begin);
    }
    if(color_matches!=1) layout.judgment_color_option=0;
    // The gameplay initializer combines the user option and forced gauge/mode
    // conditions, then stores the effective S-CRITICAL flag in GameWorld.
    // Match the complete branch sequence and validate both gauge references and
    // call destinations; ambiguous or changed code leaves this feature disabled.
    static constexpr std::string_view s_critical_guard=
        "4180be????????00752b498d8e????????e8????????833805741a498d8e????????"
        "e8????????8338077409e8????????84c07402b001418886????????84c0";
    unsigned s_critical_matches=0;
    for(const auto f:image.functions) {
        if(f.end-f.begin<s_critical_guard.size()/2 || f.end-f.begin>0x20000) continue;
        for(auto at=f.begin;at+s_critical_guard.size()/2<=f.end;++at) {
            if(image.base[at]!=0x41 || image.base[at+1]!=0x80 || image.base[at+2]!=0xbe || !matches_guard(at,s_critical_guard)) continue;
            const auto gauge=image.read<std::uint32_t>(at+0x0d),flag=image.read<std::uint32_t>(at+0x3a);
            const auto first=rel(at,0x12),second=rel(at,0x23),option=rel(at,0x2d);
            if(gauge!=image.read<std::uint32_t>(at+0x1e) || gauge<4 || flag!=gauge-4 ||
               !field(gauge) || first!=second || first<0 || option<0 || first>=image.size || option>=image.size ||
               !image.in_region(static_cast<std::uint32_t>(first),1,true) ||
               !image.in_region(static_cast<std::uint32_t>(option),1,true)) continue;
            ++s_critical_matches; layout.s_critical_flag=flag;
        }
    }
    if(s_critical_matches!=1) layout.s_critical_flag=0;
    // The NEMSYS view builds a 2D transform from two rotation sources and a
    // horizontal offset independently of the 3D camera. Derive its helper
    // from the RTTI-identified view update and verify its input ABI.
    const auto nemsys=image.vtable(".?AVGameNemesysFrameView@Game@@");
    if(nemsys && image.in_region(nemsys+8,8,false)) {
        const auto update=image.rva_of(image.read<std::uintptr_t>(nemsys+8));
        const auto f=image.function(update);
        const auto layer_inputs=matches(image,f,{0x49,0x8b,0x47,0x48,0xf3,0x0f,0x10,0x48,0x10});
        if(clean_entry(image,f) && layer_inputs.size()==3)
            layout.nemsys_update=reinterpret_cast<std::uintptr_t>(image.base+update);
        const auto calls=matches(image,f,{0x49,0x8b,0x47,0x38,0xf3,0x0f,0x10,0x48,0x04,
            0xf3,0x0f,0x59,0x0d,-1,-1,-1,-1,0xf3,0x0f,0x11,0x4c,0x24,-1,
            0x4c,0x89,0x7d,-1,0x48,0x8d,0x44,0x24,-1,0x48,0x89,0x45,-1,
            0x48,0x8d,0x44,0x24,-1,0x48,0x89,0x45,-1,0x48,0x8d,0x55,-1,
            0x48,0x8d,0x4d,-1,0xe8,-1,-1,-1,-1});
        if(calls.size()==1) {
            const auto target=rel(calls[0],54);
            if(target>=0 && target<image.size) {
                const auto helper=image.function(static_cast<std::uint32_t>(target));
                const auto abi=matches(image,helper,{0x48,0x8b,0x01,0x48,0x8b,0x50,0x48,
                    0xf3,0x0f,0x10,0x52,0x04,0xf3,0x0f,0x59,0x15,-1,-1,-1,-1,
                    0x48,0x8d,0x55,-1,0x48,0x8d,0x4d,-1,0xff,0x15,-1,-1,-1,-1,
                    0x0f,0x28,0x05,-1,-1,-1,-1,0x0f,0x11,0x45,-1,
                    0x48,0xc7,0x45,-1,0,0,0,0,0x49,0x8b,0x06,0x48,0x8b,0x48,0x38,
                    0xf3,0x0f,0x10,0x51,0x04});
                if(clean_entry(image,helper) && abi.size()==1) {
                    layout.nemsys_vtable=reinterpret_cast<std::uintptr_t>(image.base+nemsys);
                    layout.nemsys_transform=reinterpret_cast<std::uintptr_t>(image.base+target);
                }
            }
        }
    }
    // The separate JudgeLine view contains both the line and the front/base.
    // Verify the exact three source fields read by its native transform.
    const auto judge_line=image.vtable(".?AVGameJudgeLineView@Game@@");
    if(judge_line && image.in_region(judge_line+8,8,false)) {
        const auto update=image.rva_of(image.read<std::uintptr_t>(judge_line+8));
        const auto f=image.function(update);
        const auto horizontal=matches(image,f,{0x48,0x8b,0x46,0x60,0xf3,0x0f,0x10,0x30,
            0xf3,0x0f,0x59,0x35,-1,-1,-1,-1});
        const auto rotation=matches(image,f,{0x48,0x8b,0x46,0x70,0xf3,0x0f,0x10,0x50,0x04,
            0xf3,0x0f,0x59,0x15,-1,-1,-1,-1,0x48,0x8d,0x55,-1,0x48,0x8d,0x4d,-1,
            0xff,0x15,-1,-1,-1,-1,0x0f,0x28,0x05,-1,-1,-1,-1,0x0f,0x11,0x45,-1,
            0x48,0xc7,0x45,-1,0,0,0,0,0x48,0x8b,0x46,0x60,0xf3,0x0f,0x10,0x50,0x04});
        if(clean_entry(image,f) && horizontal.size()==1 && rotation.size()==1 && horizontal[0]<rotation[0]) {
            layout.judge_line_vtable=reinterpret_cast<std::uintptr_t>(image.base+judge_line);
            layout.judge_line_update=reinterpret_cast<std::uintptr_t>(image.base+update);
        }
    }
    // Cursor events derive effect direction from three independent angle
    // inputs before sampling the current camera world. Validate their ABI.
    const auto cursor=image.vtable(".?AVTumamiCursorView@Game@@");
    if(cursor && image.in_region(cursor+8,8,false)) {
        const auto event=image.rva_of(image.read<std::uintptr_t>(cursor+8));
        const auto f=image.function(event);
        const auto rotation=matches(image,f,{0x48,0x8b,0x81,0xd0,0,0,0,0x4c,0x8b,0x81,0xe0,0,0,0,
            0xf3,0x44,0x0f,0x10,0x58,0x08,0xf3,0x44,0x0f,0x58,0x58,0x04,
            0xf3,0x45,0x0f,0x58,0x18,0xf3,0x44,0x0f,0x59,0x1d,-1,-1,-1,-1});
        const auto camera=matches(image,f,{0x48,0x8b,0x49,0x08,0x48,0x8b,0x01,0x48,0x8d,0x55,-1,
            0xff,0x50,0x18,0xf3,0x0f,0x10,0x30});
        if(clean_entry(image,f) && rotation.size()==1 && camera.size()==1 && rotation[0]<camera[0]) {
            layout.cursor_vtable=reinterpret_cast<std::uintptr_t>(image.base+cursor);
            layout.cursor_event=reinterpret_cast<std::uintptr_t>(image.base+event);
        }
    }
    // Both judgment effect views read the same live angle inputs outside the
    // 3D camera matrices. Identify the methods via RTTI and their source ABI.
    const auto button=image.vtable(".?AVGameJudgeBombView@Game@@");
    if(button && image.in_region(button+8,8,false)) {
        const auto update=image.rva_of(image.read<std::uintptr_t>(button+8));
        const auto f=image.function(update);
        const auto input=matches(image,f,{0x48,0x8b,0x85,0,1,0,0,0xf3,0x0f,0x10,0x70,8,
            0xf3,0x0f,0x58,0x70,4,0xf3,0x41,0x0f,0x59,0xf3});
        if(clean_entry(image,f) && input.size()==1) {
            layout.button_bomb_vtable=reinterpret_cast<std::uintptr_t>(image.base+button);
            layout.button_bomb_update=reinterpret_cast<std::uintptr_t>(image.base+update);
        }
    }
    // Active VOL hit effects continue updating on the primary interface after
    // their creation event. This method re-reads both live camera angles.
    const auto knob_primary=image.vtable(".?AVGameJudgeTumamiBombView@Game@@");
    if(knob_primary && image.in_region(knob_primary+8,8,false)) {
        const auto update=image.rva_of(image.read<std::uintptr_t>(knob_primary+8));
        const auto f=image.function(update);
        const auto inputs=matches(image,f,{0x49,0x8b,0x87,0xd8,1,0,0,0x49,0x8b,0x97,0xe8,1,0,0,
            0xf3,0x0f,0x10,0x48,8,0xf3,0x0f,0x58,0x48,4,0xf3,0x0f,0x58,0x0a});
        if(clean_entry(image,f) && inputs.size()==1) {
            layout.knob_bomb_primary_vtable=reinterpret_cast<std::uintptr_t>(image.base+knob_primary);
            layout.knob_bomb_update=reinterpret_cast<std::uintptr_t>(image.base+update);
        }
    }
    const auto knob=image.vtable(".?AVGameJudgeTumamiBombView@Game@@",8);
    if(knob && image.in_region(knob+8,8,false)) {
        const auto event=image.rva_of(image.read<std::uintptr_t>(knob+8));
        const auto f=image.function(event);
        const auto inputs=matches(image,f,{0x49,0x8b,0x95,0xd0,1,0,0,0x49,0x8b,0x85,0xe0,1,0,0,
            0xf3,0x0f,0x10,0x4a,4,0xf3,0x0f,0x58,0x4a,8,0xf3,0x0f,0x58,8,0xf3,0x41,0x0f,0x59,0xca});
        const auto single=matches(image,f,{0x49,0x8b,0x85,0xd0,1,0,0,0xf3,0x0f,0x10,0x48,4,
            0xf3,0x0f,0x58,0x48,8,0xf3,0x41,0x0f,0x59,0xca});
        if(clean_entry(image,f) && inputs.size()==3 && single.size()==2) {
            layout.knob_bomb_vtable=reinterpret_cast<std::uintptr_t>(image.base+knob);
            layout.knob_bomb_event=reinterpret_cast<std::uintptr_t>(image.base+event);
        }
    }
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
