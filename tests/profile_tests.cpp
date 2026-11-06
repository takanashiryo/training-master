#include "native/profile_scan.hpp"
#include <windows.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <limits>
#include <stdexcept>

namespace {
using namespace training::native;
constexpr auto count=static_cast<std::size_t>(Symbol::Count);
void check(bool value,const std::string& message) { if (!value) throw std::runtime_error(message); }
template<class T> void put(std::vector<std::byte>& image,std::size_t at,T value) {
    check(at<=image.size() && sizeof(T)<=image.size()-at,"fixture overflow");
    std::memcpy(image.data()+at,&value,sizeof(value));
}
std::vector<std::byte> pattern_bytes(const Anchor& anchor) {
    std::vector<std::byte> out;
    const std::string_view text=anchor.pattern;
    for (std::size_t i=0;i<text.size();i+=2) out.push_back(text.substr(i,2)=="??"?std::byte{}:
        static_cast<std::byte>(std::stoul(std::string(text.substr(i,2)),nullptr,16)));
    return out;
}
// A synthetic link of the reviewed ABI with independently placed functions,
// globals and vtables. This is not another game release and never executes code.
struct Fixture {
    const Family& family;
    std::vector<std::byte> image=std::vector<std::byte>(0x80000);
    std::uintptr_t base;
    std::vector<std::size_t> functions;
    std::array<std::size_t,count> symbols{};
    std::size_t code{},ro{},rw{};
    std::map<unsigned,std::size_t> identities;
    std::map<std::pair<std::string,unsigned>,std::size_t> interface_tables;
    explicit Fixture(unsigned seed,const Family& selected=families().front()):family(selected),base(0x220000000ull+seed*0x10000000ull),
        code(0x1000+seed*16),ro(0x40000+seed*32),rw(0x60000+seed*48) {
        IMAGE_DOS_HEADER dos{}; dos.e_magic=IMAGE_DOS_SIGNATURE; dos.e_lfanew=0x80; put(image,0,dos);
        IMAGE_NT_HEADERS64 nt{};
        nt.Signature=IMAGE_NT_SIGNATURE; nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
        nt.FileHeader.NumberOfSections=3; nt.FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
        nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC; nt.OptionalHeader.SizeOfImage=static_cast<DWORD>(image.size());
        put(image,0x80,nt);
        for (unsigned i=0;i<3;++i) {
            IMAGE_SECTION_HEADER section{};
            section.VirtualAddress=i==0?0x1000:i==1?0x40000:0x60000;
            section.Misc.VirtualSize=i==0?0x3f000:0x20000;
            section.Characteristics=IMAGE_SCN_MEM_READ|(i==0?IMAGE_SCN_MEM_EXECUTE:i==2?IMAGE_SCN_MEM_WRITE:0);
            put(image,0x80+sizeof(nt)+i*sizeof(section),section);
        }
        for (const auto& anchor:family.anchors) {
            const auto bytes=pattern_bytes(anchor);
            functions.push_back(code);
            std::memcpy(image.data()+code,bytes.data(),bytes.size());
            if (anchor.symbol!=Symbol::None) symbols[static_cast<std::size_t>(anchor.symbol)]=code;
            code=(code+bytes.size()+31+seed*16)&~std::size_t(15);
        }
        const bool writable[]={
#define TM_SYMBOL(name, kind) std::string_view(#kind)=="Writable",
#include "native/profile_symbols.inc"
#undef TM_SYMBOL
        };
        for (std::size_t i=0;i<count;++i) if (!symbols[i]) {
            if((i==static_cast<std::size_t>(Symbol::gauge_chart_options) && !family.chart_gauge_gain) ||
               (i==static_cast<std::size_t>(Symbol::timed_stage_limit) && family.result.fixed_timed_limit) ||
               (i==static_cast<std::size_t>(Symbol::stage_data) && family.direct_stage_data) ||
               (i==static_cast<std::size_t>(Symbol::stage_data_slot) && !family.direct_stage_data)) continue;
            auto& cursor=writable[i]?rw:ro;
            symbols[i]=cursor; cursor+=256;
        }
        auto bind=[&](unsigned id,std::size_t target) {
            const auto [it,added]=identities.emplace(id,target);
            check(added || it->second==target,"inconsistent reference in test catalog");
        };
        for (std::size_t i=0;i<family.anchors.size();++i) for (const auto& ref:family.anchors[i].references) {
            if (ref.target!=Symbol::None) bind(ref.identity,symbols[static_cast<std::size_t>(ref.target)]);
            else if (ref.access==Access::Internal) bind(ref.identity,functions[i]+ref.internal);
            else if (ref.access==Access::ImageBase) bind(ref.identity,0);
        }
        for (std::size_t i=0;i<family.anchors.size();++i) for (const auto& ref:family.anchors[i].references) {
            if (!identities.contains(ref.identity)) {
                auto& cursor=ref.access==Access::Code?code:ref.access==Access::Writable?rw:ro;
                bind(ref.identity,cursor); cursor+=64;
            }
            patch(i,ref,identities.at(ref.identity));
        }
        for (auto symbol:{Symbol::world_vtable,Symbol::draw_timer_vtable,Symbol::game_timer_vtable,
                          Symbol::fumen_data_vtable,Symbol::music_select_vtable,Symbol::alternative_scene_vtable,
                          Symbol::renderer_vtable,Symbol::camera_engine_vtable,Symbol::camera_world_vtable,Symbol::camera_space_vtable,
                          Symbol::game_context_vtable,Symbol::fumen_player_vtable,Symbol::lane_range_vtable,Symbol::lane_button_vtable,Symbol::gauge_ref_vtable,Symbol::player_settings_vtable,Symbol::stage_result_vtable}) {
            const auto rva=symbols[static_cast<std::size_t>(symbol)];
            for (unsigned j=0;j<3;++j) put(image,rva+j*8,base+functions[j]);
        }
        std::map<std::string,std::size_t> descriptors;
        auto rtti=[&](const char* type,unsigned subobject,std::initializer_list<std::pair<Symbol,unsigned>> methods,Symbol table_symbol=Symbol::None) {
            auto [entry,added]=descriptors.try_emplace(type,ro);
            if(added) { std::memcpy(image.data()+ro+16,type,std::strlen(type)+1); ro=(ro+16+std::strlen(type)+8)&~std::size_t(7); }
            const auto descriptor=entry->second,locator=ro; ro+=24;
            put<std::uint32_t>(image,locator,1); put<std::uint32_t>(image,locator+4,subobject);
            put<std::uint32_t>(image,locator+12,static_cast<std::uint32_t>(descriptor));
            put<std::uint32_t>(image,locator+16,static_cast<std::uint32_t>(descriptor));
            put<std::uint32_t>(image,locator+20,static_cast<std::uint32_t>(locator));
            const auto table=table_symbol==Symbol::None?ro+8:symbols[static_cast<std::size_t>(table_symbol)];
            if(table_symbol==Symbol::None) ro+=160;
            interface_tables[{type,subobject}]=table;
            put(image,table-8,base+locator);
            for(const auto [symbol,slot]:methods) put(image,table+slot*8,base+symbols[static_cast<std::size_t>(symbol)]);
        };
        rtti(".?AVGameWorldKFC@Game@@",0,{{Symbol::world_update,3},{Symbol::collect_result,7}},Symbol::world_vtable);
        rtti(".?AVGameTimer@Game@@",0,{{Symbol::timer_update,7}},Symbol::game_timer_vtable);
        rtti(".?AVCBaseScene@@",0,{{Symbol::request_next,11}});
        rtti(".?AVCStageResultGameData@@",0,{{Symbol::serialize_result,14}},Symbol::stage_result_vtable);
        rtti(".?AVCPlayerSettingGameData@@",0,{},Symbol::player_settings_vtable);
        rtti(".?AVCameraUpdater@Game@@",8,{{Symbol::camera_update,1}});
        rtti(".?AVGamePostEffectView@Game@@",8,{{Symbol::posteffect_event,1}});
        rtti(".?AV?$_Func_impl_no_alloc@V<lambda_ab441f34bf477c931a8250d5bfec0a5a>@@_NM@std@@",0,{{Symbol::posteffect_shake,2}});
        rtti(".?AVHispUpdater@Game@@",0,{{Symbol::hisp_update,1}});
        rtti(".?AVHispUpdater@Game@@",8,{{Symbol::hisp_event,1}});
        rtti(".?AVGameHudView@Game@@",8,{{Symbol::gauge_hud_event,1}});
        rtti(".?AV?$TimeStretchAudioFeeder@M$01@BMSoundLib2017@@",0,{{Symbol::audio_feed,1}});
        check(code<0x40000 && ro<0x60000 && rw<image.size(),"fixture section overflow");
    }
    void patch(std::size_t index,const Reference& ref,std::size_t target) {
        const auto value=ref.kind==ReferenceKind::ImageRelative32?static_cast<std::int64_t>(target):
            static_cast<std::int64_t>(target)-static_cast<std::int64_t>(functions[index]+ref.next);
        put(image,functions[index]+ref.operand,static_cast<std::uint32_t>(value));
    }
    std::size_t anchor(std::string_view name) const {
        for (std::size_t i=0;i<family.anchors.size();++i) if (family.anchors[i].name==name) return i;
        throw std::runtime_error("test anchor absent");
    }
    Resolution resolve() const {
        Resolution out; std::string reason;
        check(resolve_image(image,base,family,out,reason),reason);
        return out;
    }
    void rejects(std::string_view expected) const {
        Resolution out=resolve_sentinel(); std::string reason;
        check(!resolve_image(image,base,family,out,reason),"invalid image accepted");
        check(out.family.empty() && out.addresses.world_update==0,"failed resolution leaked stale RVAs");
        check(reason.find(expected)!=std::string::npos,"unexpected rejection: "+reason);
    }
    static Resolution resolve_sentinel() { Resolution out; out.family="stale"; out.addresses.world_update=123; return out; }
};
}
int main() {
    try {
        for(const auto& family:families()) {
            Fixture relocated(5,family);
            const auto result=relocated.resolve();
            check(result.camera && result.camera.position==std::array<float,3>{} && result.camera.rotation==std::array<float,3>{},
                  "camera baseline must be read from relocated native data, not hardcoded reference values");
            {
                Fixture defaults(9,family);
                const auto world=defaults.anchor("camera_world_constructor");
                const auto& refs=family.anchors[world].references;
                const auto first=std::find_if(refs.begin(),refs.end(),[](const auto& ref) { return ref.access==Access::Writable; });
                check(first!=refs.end(),"camera defaults fixture requires native vector data");
                const auto target=defaults.identities.at(first->identity);
                put<float>(defaults.image,target,17.25f);
                check(defaults.resolve().camera.position[0]==17.25f,"camera baseline must follow the constructor's own data reference");
                put<float>(defaults.image,target,std::numeric_limits<float>::quiet_NaN());
                defaults.rejects("Camera defaults: invalid native values");
                put<float>(defaults.image,target,0.f);
                defaults.patch(world,*first,defaults.image.size()-1);
                defaults.rejects("Camera defaults: vector outside native data");
            }
            check(result.interface_methods==12,"all audited RTTI method contracts must bind");
            check(result.world==family.world,"resolved ABI must carry its field layout");
            check(result.result==family.result && result.chart_gauge_gain==family.chart_gauge_gain,"resolved record/interface ABI differs");
            for(const auto& other:families()) if(&other!=&family) {
                Resolution rejected; std::string reason;
                check(!resolve_image(relocated.image,relocated.base,other,rejected,reason),"foreign layout accepted");
            }
            const auto selector_index=relocated.anchor("track_options");
            Resolution complete; std::string complete_reason;
            check(resolve_image(relocated.image,relocated.base,complete,complete_reason) && complete.world==family.world,"runtime and family entry points must agree");
            if(!family.anchors[selector_index].selectors.empty()) {
                const auto selector=family.anchors[selector_index].selectors.front();
                const auto selector_start=relocated.functions[selector_index];
                put<std::uint32_t>(relocated.image,selector_start+selector.maximum,250);
                put<std::uint32_t>(relocated.image,selector_start+selector.exclusive,251);
                put<std::uint32_t>(relocated.image,selector_start+selector.fallback,177);
                const auto changed=relocated.resolve();
                check(std::any_of(changed.details.begin(),changed.details.end(),[](const auto& x) { return x.find("Compatible content selector")!=std::string::npos; }),"content change must be diagnosed");
                put<std::uint32_t>(relocated.image,selector_start+selector.exclusive,252); relocated.rejects("track_options");
                put<std::uint32_t>(relocated.image,selector_start+selector.exclusive,251);
                put<std::uint32_t>(relocated.image,selector_start+selector.fallback,251); relocated.rejects("track_options");
                put<std::uint32_t>(relocated.image,selector_start+selector.fallback,177);
                put<std::uint32_t>(relocated.image,selector_start+selector.maximum,256);
                put<std::uint32_t>(relocated.image,selector_start+selector.exclusive,257); relocated.rejects("track_options");
                put<std::uint32_t>(relocated.image,selector_start+selector.maximum,250);
                put<std::uint32_t>(relocated.image,selector_start+selector.exclusive,251);
            }
            const auto index=relocated.anchor("play_state");
            const auto patch=family.anchors[index].patches.front();
            const auto site=relocated.functions[index]+patch.offset;
            relocated.image[site]=std::byte{0x90}; relocated.image[site+1]=std::byte{0xe9};
            relocated.resolve();
            relocated.image[site+2]^=std::byte{1}; relocated.rejects("play_state");
        }
        Fixture first(1),shifted(9);
        const auto a=first.resolve(),b=shifted.resolve();
#define TM_SYMBOL(name, kind) \
        check(a.addresses.name==first.symbols[static_cast<std::size_t>(Symbol::name)],"first relocation: " #name); \
        check(b.addresses.name==shifted.symbols[static_cast<std::size_t>(Symbol::name)],"second relocation: " #name); \
        check((Symbol::name==Symbol::stage_data_slot && !a.addresses.name && !b.addresses.name) || a.addresses.name!=b.addresses.name,"symbol must move: " #name);
#include "native/profile_symbols.inc"
#undef TM_SYMBOL
        first.image[0x5fff0]=std::byte{0x55}; first.resolve(); // Unrelated patch has no version/hash gate.
        {
            auto timer_freeze=first;
            const auto state=timer_freeze.functions[timer_freeze.anchor("play_state")];
            check(timer_freeze.image[state+0x4a]==std::byte{0x0f} &&
                  timer_freeze.image[state+0x4b]==std::byte{0x84},"native Premium Time branch fixture changed");
            // Spice2x Premium Free Timer Freeze changes JZ rel32 to NOP/JMP
            // with the same displacement. This is not a hook or ABI change.
            timer_freeze.image[state+0x4a]=std::byte{0x90};
            timer_freeze.image[state+0x4b]=std::byte{0xe9};
            const auto result=timer_freeze.resolve();
            check(result.details.front().find("Premium Free Timer Freeze")!=std::string::npos,"accepted patch should be diagnosed");
            timer_freeze.image[state+0x4b]=std::byte{0x84};
            timer_freeze.rejects("play_state"); // A partial or arbitrary patch is not accepted.
            timer_freeze.image[state+0x4b]=std::byte{0xe9};
            timer_freeze.image[state+0x4c]^=std::byte{1};
            timer_freeze.rejects("play_state"); // Preserve the original branch target.
        }
        {
            auto broken=first;
            broken.image[broken.symbols[static_cast<std::size_t>(Symbol::world_update)]]=std::byte{0xe9};
            broken.rejects("world_update");
        }
        {
            auto broken=first;
            const auto world=broken.symbols[static_cast<std::size_t>(Symbol::world_vtable)];
            put(broken.image,world+24,broken.base+broken.symbols[static_cast<std::size_t>(Symbol::timer_update)]);
            broken.rejects("world_update"); // A valid function in the wrong native slot is not accepted.
        }
        {
            auto broken=first;
            const auto table=broken.interface_tables.at({".?AVHispUpdater@Game@@",8});
            put(broken.image,table+8,broken.base+broken.symbols[static_cast<std::size_t>(Symbol::hisp_update)]);
            broken.rejects("hisp_event"); // Primary and secondary this pointers are distinct contracts.
        }
        {
            auto broken=first; const auto i=broken.anchor("track_options");
            const auto offset=broken.family.anchors[i].selectors.front().maximum;
            broken.image[broken.functions[i]+offset-1]^=std::byte{1};
            broken.rejects("track_options"); // Only the reviewed immediate is variable, never its opcode.
        }
        {
            auto broken=first;
            constexpr char type[]=".?AVGameWorldKFC@Game@@";
            std::memcpy(broken.image.data()+0x5ff10,type,sizeof(type));
            broken.rejects("RTTI descriptor");
        }
        {
            auto broken=first; const auto index=broken.anchor("stage_record_prefix");
            const auto offset=std::string_view(broken.family.anchors[index].pattern).find("28030000");
            check(offset!=std::string_view::npos,"result hash layout fixture missing");
            broken.image[broken.functions[index]+offset/2]=std::byte{0x20};
            broken.rejects("stage_record_prefix");
        }
        {
            auto broken=first; const auto index=broken.anchor("timer_seek");
            const auto position=std::string_view(broken.family.anchors[index].pattern).find("807928");
            check(position!=std::string_view::npos,"timer member fixture missing");
            broken.image[broken.functions[index]+position/2+2]=std::byte{0x29};
            broken.rejects("timer_seek");
        }
        {
            auto broken=first; const auto index=broken.anchor("audio_graph_constructor");
            const auto position=std::string_view(broken.family.anchors[index].pattern).find("44ac0000");
            check(position!=std::string_view::npos,"native 44100 Hz music bus format guard missing");
            broken.image[broken.functions[index]+position/2]=std::byte{0x80};
            broken.image[broken.functions[index]+position/2+1]=std::byte{0xbb}; // 48000 Hz
            broken.rejects("audio_graph_constructor");
        }
        {
            auto broken=first;
            const auto index=broken.anchor("can_continue");
            for (const auto& ref:broken.family.anchors[index].references) if (ref.target==Symbol::game_mode) {
                broken.patch(index,ref,0x40000); break;
            }
            broken.rejects("reference");
        }
        {
            auto broken=first;
            const auto index=broken.anchor("flow_decision");
            for (const auto& ref:broken.family.anchors[index].references) if (ref.target==Symbol::stage_data) {
                broken.patch(index,ref,broken.symbols[static_cast<std::size_t>(Symbol::timer_update)]); break;
            }
            broken.rejects("reference");
        }
        {
            auto broken=first;
            put(broken.image,broken.symbols[static_cast<std::size_t>(Symbol::world_vtable)],std::uintptr_t{1});
            broken.rejects("vtable slot");
        }
        {
            auto broken=first; const auto index=broken.anchor("flow_decision");
            for (const auto& ref:broken.family.anchors[index].references) if (ref.access==Access::Internal) {
                broken.patch(index,ref,broken.functions[index]+ref.internal+1); break;
            }
            broken.rejects("internal dispatch table");
        }
        {
            auto broken=first; const auto index=broken.anchor("world_update");
            const auto& ref=broken.family.anchors[index].references.front();
            broken.patch(index,ref,broken.image.size()+16);
            broken.rejects("outside module");
        }
        {
            auto duplicate=first;
            const auto index=duplicate.anchor("stage_data");
            const auto size=std::strlen(duplicate.family.anchors[index].pattern)/2;
            // An identical getter elsewhere is disambiguated by its real callers.
            std::memcpy(duplicate.image.data()+duplicate.code,duplicate.image.data()+duplicate.functions[index],size);
            for (const auto& ref:duplicate.family.anchors[index].references) {
                const auto target=duplicate.identities.at(ref.identity);
                const auto relative=static_cast<std::int64_t>(target)-static_cast<std::int64_t>(duplicate.code+ref.next);
                put(duplicate.image,duplicate.code+ref.operand,static_cast<std::uint32_t>(relative));
            }
            duplicate.resolve();
        }
        {
            auto ambiguous=first;
            std::vector<Anchor> anchors(ambiguous.family.anchors.begin(),ambiguous.family.anchors.end());
            const Anchor extra{"unconnected_ambiguous",Symbol::None,"0102030405060708090a0b0c0d0e0f10",{}};
            const auto bytes=pattern_bytes(extra);
            std::memcpy(ambiguous.image.data()+ambiguous.code,bytes.data(),bytes.size());
            std::memcpy(ambiguous.image.data()+ambiguous.code+64,bytes.data(),bytes.size());
            anchors.push_back(extra); Family family{"ambiguous-test",anchors};
            Resolution out; std::string reason;
            check(!resolve_image(ambiguous.image,ambiguous.base,family,out,reason) &&
                reason.find("ambiguous signature")!=std::string::npos,"unresolved ambiguity must be rejected");
        }
        {
            auto broken=first; put(broken.image,0x3c,std::int32_t{-1}); broken.rejects("DOS header");
            auto truncated=first; truncated.image.resize(0x90); truncated.rejects("Truncated");
        }
        std::cout<<"Dynamic profile relocation, reference-graph and ABI rejection scenarios passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
