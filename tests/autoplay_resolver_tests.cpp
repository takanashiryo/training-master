#include "game/autoplay_resolver.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace {
struct Fixture {
    unsigned char* data = static_cast<unsigned char*>(VirtualAlloc(nullptr,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    std::uint32_t metadata = 0xA000, update, load, judge, clock, world_table{}, referee_table{};
    std::uint32_t ctx_offset, flags_offset, timer_offset;
    std::vector<RUNTIME_FUNCTION> functions;
    template<class T> void put(std::uint32_t at, T value) { std::memcpy(data+at,&value,sizeof(value)); }
    void bytes(std::uint32_t at, std::initializer_list<unsigned char> value) { for(auto b:value) data[at++]=b; }
    std::uint32_t reserve(std::uint32_t count) { auto at=metadata; metadata=(metadata+count+7)&~7u; return at; }
    std::uint32_t rtti(const char* name, std::initializer_list<std::uint32_t> methods) {
        const auto type=reserve(static_cast<std::uint32_t>(16+std::strlen(name)+1));
        std::memcpy(data+type+16,name,std::strlen(name)+1);
        const auto hierarchy=reserve(16), col=reserve(24);
        put<std::uint32_t>(col,1); put(col+12,type); put(col+16,hierarchy); put(col+20,col);
        const auto table=reserve(static_cast<std::uint32_t>(16+methods.size()*8));
        put<std::uintptr_t>(table,reinterpret_cast<std::uintptr_t>(data+col));
        unsigned i=1;
        for(auto method:methods) put<std::uintptr_t>(table+8*i++,reinterpret_cast<std::uintptr_t>(data+method));
        return table+8;
    }
    void sync_functions() {
        auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(data+0x80);
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION]={0xF000,static_cast<DWORD>(functions.size()*sizeof(RUNTIME_FUNCTION))};
        std::memcpy(data+0xF000,functions.data(),functions.size()*sizeof(RUNTIME_FUNCTION));
    }
    explicit Fixture(bool moved=false) : update(moved?0x5100:0x1100), load(update+0x200), judge(update+0x500), clock(update+0x800),
        ctx_offset(moved?0x1D8:0xA8), flags_offset(moved?0x68:0x58), timer_offset(moved?0x220:0xD8) {
        if(!data) throw std::runtime_error("VirtualAlloc failed");
        std::memset(data+0x1000,0x90,0x8000);
        IMAGE_DOS_HEADER dos{}; dos.e_magic=IMAGE_DOS_SIGNATURE; dos.e_lfanew=0x80; put(0,dos);
        IMAGE_NT_HEADERS64 nt{}; nt.Signature=IMAGE_NT_SIGNATURE;
        nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64; nt.FileHeader.NumberOfSections=3;
        nt.FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
        nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC; nt.OptionalHeader.SizeOfImage=0x10000;
        nt.OptionalHeader.NumberOfRvaAndSizes=IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        put(0x80,nt);
        const auto sections=0x80+sizeof(nt);
        for(unsigned i=0;i<3;++i) {
            IMAGE_SECTION_HEADER section{};
            section.VirtualAddress=i==0?0x1000:i==1?0xA000:0xF000;
            section.Misc.VirtualSize=i==0?0x8000:i==1?0x5000:0x1000;
            section.Characteristics=IMAGE_SCN_MEM_READ|(i==0?IMAGE_SCN_MEM_EXECUTE:0);
            put(static_cast<std::uint32_t>(sections+i*sizeof(section)),section);
        }
        bytes(update,{0x48,0x89,0x5C,0x24,0x10});
        bytes(update+24,{0x48,0x8B,0x89,0,0,0,0,0x48,0x85,0xC9,0x74,0x05,0x48,0x8B,0x01,0xFF,0x50,0x38});
        put(update+27,timer_offset);
        bytes(update+64,{0x48,0xB8,0xDB,0x34,0xB6,0xD7,0x82,0xDE,0x1B,0x43,0xC3});
        bytes(load,{0x4C,0x8B,0xE9});
        bytes(load+32,{0x4D,0x8D,0xA5,0,0,0,0,0x48,0x8B,0xD0,0x49,0x8B,0xCC,0xE8,0,0,0,0});
        put(load+35,ctx_offset);
        bytes(load+80,{0x49,0x8B,0x14,0x24,0x48,0x83,0xC2,0,0x48,0x8D,0x4D,0xA0,0xE8,0,0,0,0,0xBE,0x80,0,0,0,0x48,0x8B,0x45,0xA0,0xC6,0,1,0xC3});
        data[load+87]=static_cast<unsigned char>(flags_offset);
        bytes(judge,{0x48,0x8B,0xC4,0x0F,0x28,0xF3,0xC7,0x03,6,0,0,0,0xC7,0x03,5,0,0,0,
            0x48,0xB8,0x67,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0xC3});
        const unsigned char tbase=moved?0x30:0x20, toffset=moved?0x34:0x24, tenabled=moved?0x38:0x28;
        bytes(clock,{0x80,0x79,tenabled,0,0x74,0x0B,0xF3,0x0F,0x10,0x41,toffset,
            0xF3,0x0F,0x58,0x41,tbase,0xC3,0x0F,0x57,0xC0,0xF3,0x0F,0x58,0x41,tbase,0xC3});
        functions={{update,update+128,0},{load,load+160,0},{judge,judge+128,0}};
        sync_functions();
        // Method order deliberately differs from the game's current vtable slots.
        world_table=rtti(".?AVGameWorldKFC@Game@@",{load,update});
        referee_table=rtti(".?AVNoteReferee@Game@@",{judge});
        rtti(".?AVGameContext@Game@@",{clock}); rtti(".?AVGameTimer@Game@@",{clock});
    }
    ~Fixture() { VirtualFree(data,0,MEM_RELEASE); }
    Fixture(const Fixture&)=delete;
    training::autoplay_discovery::Result resolve() { return training::autoplay_discovery::resolve(reinterpret_cast<HMODULE>(data)); }
};
bool exact(const Fixture& f, const training::autoplay_discovery::Result& r) {
    return r && r.layout.update==reinterpret_cast<std::uintptr_t>(f.data+f.update) &&
        r.layout.judge==reinterpret_cast<std::uintptr_t>(f.data+f.judge) && r.layout.context==f.ctx_offset &&
        r.layout.flags==f.flags_offset && r.layout.timer==f.timer_offset;
}
}

int main() {
    unsigned failed=0, total=0;
    auto check=[&](const char* label, bool good) { ++total; if(!good) ++failed; std::printf("%s %s\n",good?"PASS":"FAIL",label); };
    { Fixture f; check("primary RTTI and nonstandard method order",exact(f,f.resolve())); }
    { Fixture f(true); auto r=f.resolve(); check("moved RVAs, relocated mapping and changed object fields",
        exact(f,r)&&r.layout.time_base==0x30&&r.layout.time_offset==0x34&&r.layout.time_enabled==0x38); }
    { Fixture f; f.data[f.update]=0xE9; check("existing update detour rejected",!f.resolve()); }
    { Fixture f; f.data[f.judge]=0xE9; check("existing judge detour rejected",!f.resolve()); }
    { Fixture f; f.bytes(f.judge,{0x50,0x48,0xB8,1,2,3,4,5,6,7,8,0x50,0xC3});
      check("push/return trampoline rejected",!f.resolve()); }
    { Fixture f; f.data[f.judge+8]=7; check("changed judge result ABI rejected",!f.resolve()); }
    { Fixture f; f.put<std::uint32_t>(f.load+35,0xFFFFFFF0); check("negative context offset rejected",!f.resolve()); }
    { Fixture f; f.data[f.load+87]=0xF8; check("negative signed flags add rejected",!f.resolve()); }
    { Fixture f; f.data[f.clock+24]=0x30; check("inconsistent timer branch rejected",!f.resolve()); }
    { Fixture f; f.data[f.clock+2]=0x22; check("timer flag overlapping float rejected",!f.resolve()); }
    { Fixture f; f.rtti(".?AVGameWorldKFC@Game@@",{f.load,f.update}); check("duplicate RTTI rejected",!f.resolve()); }
    { Fixture f; auto copy=f.judge+0x100; std::memcpy(f.data+copy,f.data+f.judge,128);
      f.functions.push_back({copy,copy+128,0}); f.sync_functions();
      f.put<std::uintptr_t>(f.referee_table+8,reinterpret_cast<std::uintptr_t>(f.data+copy));
      check("ambiguous judge methods rejected",!f.resolve()); }
    { Fixture f; f.put<std::uintptr_t>(f.world_table,UINTPTR_MAX); check("out-of-image method rejected",!f.resolve()); }
    { Fixture f; f.put<std::uint16_t>(0x80+4,IMAGE_FILE_MACHINE_I386); check("wrong architecture rejected",!f.resolve()); }
    { Fixture f; f.put<std::uint32_t>(0x80+24+56,0xFFFF); // PE SizeOfImage ends before final section.
      check("truncated image rejected",!f.resolve()); }
    check("null module rejected",!training::autoplay_discovery::resolve(nullptr));
    std::printf("%u/%u passed\n",total-failed,total);
    return failed?1:0;
}
