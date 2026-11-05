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
        const auto gate=judge+0x200;
        bytes(gate,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0x0A,0x48,0x8B,0x01,0xFF,0x50,0x38,
            0x48,0xBA,0xB3,1,0,0,0,1,0,0,0x8B,0x01,0xFF,0xC8,0x83,0xF8,9,0x48,0x83,0xC1,0x28,0xC3});
        functions.push_back({gate,gate+128,0}); sync_functions();
        referee_table=rtti(".?AVNoteReferee@Game@@",{judge,gate});
        rtti(".?AVGameContext@Game@@",{clock}); rtti(".?AVGameTimer@Game@@",{clock});
    }
    ~Fixture() { VirtualFree(data,0,MEM_RELEASE); }
    Fixture(const Fixture&)=delete;
    training::autoplay_discovery::Result resolve() { return training::autoplay_discovery::resolve(reinterpret_cast<HMODULE>(data)); }
    void input() {
        const auto checker=update+0xc00,encoder=update+0xe00;
        bytes(checker,{0x53,0x48,0x83,0xec,0x40});
        unsigned i=0;
        for(unsigned char offset:{0x68,0x7c,0x80}) {
            const auto at=checker+32+32*i++;
            bytes(at,{0x48,0x8b,0x0d,0,0,0,0,0x83,0xb9,offset,0x81,0,0,0});
            put<std::int32_t>(at+3,static_cast<std::int32_t>(0xe800-at-7));
        }
        bytes(checker+128,{0xc6,0x43,0x10,1,0xf3,0x0f,0x58,0x73,0x14,0xf3,0x0f,0x11,0x73,0x14,
            0xc6,0x43,0x10,0,0xc7,0x43,0x14,0,0,0,0});
        bytes(encoder,{0x53,0x48,0x83,0xec,0x40});
        bytes(encoder+32,{0x48,0x8b,0x15,0,0,0,0,0x8b,0x4d,0,0x48,0x83,0xf9,2,0x0f,0x83,0,0,0,0,
            0x0f,0xb7,0x84,0xca,0x5c,0x81,0,0,0x66,0x2b,0x84,0xca,0x58,0x81,0,0,0x0f,0xbf,0xf8});
        put<std::int32_t>(encoder+35,static_cast<std::int32_t>(0xe800-encoder-39));
        functions.push_back({checker,checker+160,0}); functions.push_back({encoder,encoder+128,0}); sync_functions();
        rtti(".?AVGameInputCheckRequestForceTrackCrash@Game@@",{clock,checker});
    }
};
bool exact(const Fixture& f, const training::autoplay_discovery::Result& r) {
    return r && r.layout.update==reinterpret_cast<std::uintptr_t>(f.data+f.update) &&
        r.layout.judge==reinterpret_cast<std::uintptr_t>(f.data+f.judge) && r.layout.context==f.ctx_offset &&
        r.layout.flags==f.flags_offset && r.layout.timer==f.timer_offset &&
        r.layout.note_gate==reinterpret_cast<std::uintptr_t>(f.data+f.judge+0x200);
}
}

int main(int argc,char** argv) {
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
    for(bool moved:{false,true}) {
        Fixture f(moved); f.input(); const auto r=f.resolve();
        check("game input slot resolved from relocated native button and encoder reads",r &&
              r.layout.input_device_slot==reinterpret_cast<std::uintptr_t>(f.data+0xe800) &&
              r.layout.input_check_update==reinterpret_cast<std::uintptr_t>(f.data+f.update+0xc00) && r.layout.input_check_vtable);
    }
    { Fixture f; f.input(); f.data[f.update+0xc00+41]=0x69;
      const auto r=f.resolve(); check("changed button ABI disables only shortcuts",r && !r.layout.input_device_slot); }
    { Fixture f; f.input(); f.put<std::int32_t>(f.update+0xc00+67,0);
      const auto r=f.resolve(); check("inconsistent native IO slots disable shortcuts",r && !r.layout.input_device_slot); }
    { Fixture f; f.input(); f.data[f.update+0xe00+56]=0x60;
      const auto r=f.resolve(); check("changed encoder ABI disables shortcuts",r && !r.layout.input_device_slot); }
    { Fixture f; f.input(); f.data[f.update+0xc00]=0xe9;
      const auto r=f.resolve(); check("detoured button checker rejected",r && !r.layout.input_device_slot); }
    { Fixture f; f.input(); f.data[f.update+0xc00+130]=0x18;
      const auto r=f.resolve(); check("changed force-crash request ABI disables shortcuts",r && !r.layout.input_device_slot && !r.layout.input_check_update); }
    { Fixture f; f.input(); f.put<std::int32_t>(f.update+0xe00+35,0);
      const auto r=f.resolve(); check("encoder must use the same IO singleton as buttons",r && !r.layout.input_device_slot); }
    if(argc==2) {
        const auto module=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        const auto r=training::autoplay_discovery::resolve(module);
        check("reference game input ABI verified without running game code",r && r.layout.input_device_slot);
        if(module) FreeLibrary(module);
    }
    { Fixture f; f.data[f.judge+0x200+19]=0;
      const auto r=f.resolve(); check("modified note gate disables selective dispatch",r && !r.layout.note_gate); }
    { Fixture f; const auto copy=f.judge+0x400;
      std::memcpy(f.data+copy,f.data+f.judge+0x200,128);
      f.functions.push_back({copy,copy+128,0}); f.sync_functions();
      f.put<std::uintptr_t>(f.referee_table+16,reinterpret_cast<std::uintptr_t>(f.data+copy));
      const auto r=f.resolve(); check("ambiguous note gates disabled",r && !r.layout.note_gate); }
    std::printf("%u/%u passed\n",total-failed,total);
    return failed?1:0;
}
