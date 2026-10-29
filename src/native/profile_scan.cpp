#include "native/profile_scan.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace training::native {
namespace {
enum class Kind { Code, Vtable, Vtable2, Writable };
constexpr std::size_t symbol_count=static_cast<std::size_t>(Symbol::Count);
constexpr Kind kinds[]={
#define TM_SYMBOL(name, kind) Kind::kind,
#include "native/profile_symbols.inc"
#undef TM_SYMBOL
};
constexpr std::string_view names[]={
#define TM_SYMBOL(name, kind) #name,
#include "native/profile_symbols.inc"
#undef TM_SYMBOL
};
#include "native/profile_catalog.inc"

template<class T> T read(std::span<const std::byte> image,std::size_t offset) {
    if (offset>image.size() || sizeof(T)>image.size()-offset) throw std::runtime_error("Truncated PE/reference");
    T value; std::memcpy(&value,image.data()+offset,sizeof(value)); return value;
}
struct Section { std::size_t start,size; DWORD flags; };
struct Image {
    std::span<const std::byte> bytes;
    std::uintptr_t base;
    std::vector<Section> sections;
    explicit Image(std::span<const std::byte> data,std::uintptr_t address):bytes(data),base(address) {
        const auto dos=read<IMAGE_DOS_HEADER>(bytes,0);
        if (dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0) throw std::runtime_error("Invalid DOS header");
        const auto offset=static_cast<std::size_t>(dos.e_lfanew);
        const auto nt=read<IMAGE_NT_HEADERS64>(bytes,offset);
        if (nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER64) ||
            !nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections>96 ||
            nt.OptionalHeader.SizeOfImage>bytes.size() || nt.OptionalHeader.SizeOfImage<sizeof(nt) ||
            bytes.size()>std::numeric_limits<std::uintptr_t>::max()-base)
            throw std::runtime_error("Unsupported or truncated x64 PE layout");
        bytes=bytes.first(nt.OptionalHeader.SizeOfImage);
        const auto table=offset+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader;
        for (unsigned i=0;i<nt.FileHeader.NumberOfSections;++i) {
            const auto section=read<IMAGE_SECTION_HEADER>(bytes,table+i*sizeof(IMAGE_SECTION_HEADER));
            const auto start=section.VirtualAddress,size=section.Misc.VirtualSize;
            if (!size || start>bytes.size() || size>bytes.size()-start) throw std::runtime_error("Invalid section bounds");
            for (const auto& other:sections)
                if (start<other.start+other.size && other.start<static_cast<std::size_t>(start)+size)
                    throw std::runtime_error("Overlapping PE sections");
            sections.push_back({start,size,section.Characteristics});
        }
    }
    bool has(std::size_t rva,std::size_t size,Access access) const {
        if (access==Access::ImageBase) return rva==0;
        for (const auto& s:sections) if (rva>=s.start && size<=s.size && rva-s.start<=s.size-size) {
            if (!(s.flags&IMAGE_SCN_MEM_READ)) return false;
            const bool exec=(s.flags&IMAGE_SCN_MEM_EXECUTE)!=0,write=(s.flags&IMAGE_SCN_MEM_WRITE)!=0;
            return access==Access::Code?exec&&!write:access==Access::Writable?write&&!exec:!exec&&!write;
        }
        return false;
    }
};
struct Pattern {
    struct Variant {
        std::size_t offset;
        std::vector<std::byte> original,replacement;
        std::string name;
    };
    std::vector<std::byte> bytes;
    std::vector<bool> fixed;
    std::vector<Variant> variants;
    std::vector<SelectorBounds> selectors;
    std::size_t key_start{},key_length{};
    explicit Pattern(const Anchor& anchor) {
        const std::string_view text=anchor.pattern;
        auto hex=[](char ch)->int {
            if (ch>='0' && ch<='9') return ch-'0';
            if (ch>='a' && ch<='f') return ch-'a'+10;
            if (ch>='A' && ch<='F') return ch-'A'+10;
            throw std::runtime_error("Invalid catalog hex digit");
        };
        if (text.empty() || text.size()%2 || text.size()>262144) throw std::runtime_error("Invalid catalog pattern size");
        for (std::size_t i=0;i<text.size();i+=2) {
            const bool wildcard=text.substr(i,2)=="??";
            bytes.push_back(wildcard?std::byte{}:static_cast<std::byte>((hex(text[i])<<4)|hex(text[i+1])));
            fixed.push_back(!wildcard);
        }
        auto decode=[&](std::string_view value) {
            if (value.empty() || value.size()%2 || value.size()>64) throw std::runtime_error("Invalid patch alternative size");
            std::vector<std::byte> decoded;
            for (std::size_t i=0;i<value.size();i+=2) decoded.push_back(static_cast<std::byte>((hex(value[i])<<4)|hex(value[i+1])));
            return decoded;
        };
        for (const auto& patch:anchor.patches) {
            Variant variant{patch.offset,decode(patch.original),decode(patch.replacement),std::string(patch.name)};
            const auto size=variant.original.size();
            if (patch.offset<32 || size!=variant.replacement.size() || patch.offset>bytes.size() || size>bytes.size()-patch.offset)
                throw std::runtime_error("Patch alternative overlaps entry or exceeds function bounds");
            for (std::size_t i=0;i<size;++i) {
                if (!fixed[patch.offset+i] || bytes[patch.offset+i]!=variant.original[i])
                    throw std::runtime_error("Patch alternative does not match fixed ABI bytes");
                fixed[patch.offset+i]=false;
            }
            variants.push_back(std::move(variant));
        }
        if(!anchor.selectors.empty() && (anchor.symbol!=Symbol::track_options || anchor.selectors.size()!=1))
            throw std::runtime_error("Unreviewed content-selector normalization");
        for(const auto& selector:anchor.selectors) {
            for(const auto offset:{selector.maximum,selector.exclusive,selector.fallback}) {
                if(offset<32 || offset>bytes.size() || bytes.size()-offset<4) throw std::runtime_error("Selector operand outside function");
                for(std::size_t j=0;j<4;++j) {
                    if(!fixed[offset+j]) throw std::runtime_error("Selector operand overlaps address or patch");
                    fixed[offset+j]=false;
                }
            }
            const auto maximum=read<std::uint32_t>(bytes,selector.maximum);
            if(maximum>255 || read<std::uint32_t>(bytes,selector.exclusive)!=maximum+1 ||
               read<std::uint32_t>(bytes,selector.fallback)>maximum) throw std::runtime_error("Invalid reviewed selector bounds");
            selectors.push_back(selector);
        }
        std::size_t run{};
        for (std::size_t i=0;i<bytes.size();++i) {
            run=fixed[i]?run+1:0;
            if (run>key_length && key_length<32) { key_length=run; key_start=i+1-run; }
        }
        if (key_length<8) throw std::runtime_error("Catalog anchor has insufficient fixed bytes");
    }
    bool matches(std::span<const std::byte> image,std::size_t at) const {
        if (at>image.size() || bytes.size()>image.size()-at) return false;
        for (std::size_t i=0;i<bytes.size();++i) if (fixed[i] && bytes[i]!=image[at+i]) return false;
        for (const auto& variant:variants) {
            const auto begin=image.begin()+at+variant.offset;
            if (!std::equal(variant.original.begin(),variant.original.end(),begin) &&
                !std::equal(variant.replacement.begin(),variant.replacement.end(),begin)) return false;
        }
        for(const auto& selector:selectors) {
            const auto maximum=read<std::uint32_t>(image,at+selector.maximum);
            if(maximum>255 || read<std::uint32_t>(image,at+selector.exclusive)!=maximum+1 ||
               read<std::uint32_t>(image,at+selector.fallback)>maximum) return false;
        }
        return true;
    }
};
// Audited virtual interface contracts shared by every supplied NABLA sample.
struct RttiMethod { Symbol symbol; unsigned slot; };
struct RttiInterface {
    const char* type; unsigned subobject;
    std::span<const RttiMethod> methods; Symbol table{Symbol::None}; bool alternative{};
};
constexpr RttiMethod world_methods[]={{Symbol::world_update,3},{Symbol::collect_result,7}};
constexpr RttiMethod timer_methods[]={{Symbol::timer_update,7}};
constexpr RttiMethod scene_methods[]={{Symbol::request_next,11}};
constexpr RttiMethod result_methods[]={{Symbol::serialize_result,14}};
constexpr RttiMethod camera_methods[]={{Symbol::camera_update,1}};
constexpr RttiMethod hisp_methods[]={{Symbol::hisp_update,1}};
constexpr RttiMethod hisp_events[]={{Symbol::hisp_event,1}};
constexpr RttiMethod hud_events[]={{Symbol::gauge_hud_event,1}};
constexpr RttiMethod audio_methods[]={{Symbol::audio_feed,1}};
constexpr RttiInterface interfaces[]={
    {".?AVGameWorldKFC@Game@@",0,world_methods,Symbol::world_vtable},
    {".?AVGameTimer@Game@@",0,timer_methods,Symbol::game_timer_vtable},
    {".?AVCBaseScene@@",0,scene_methods},
    {".?AVCStageResultGameData@@",0,result_methods},
    {".?AVCPlayerSettingGameData@@",0,{},Symbol::player_settings_vtable},
    {".?AVCameraUpdater@Game@@",8,camera_methods},
    {".?AVHispUpdater@Game@@",0,hisp_methods},
    {".?AVHispUpdater@Game@@",8,hisp_events},
    {".?AVGameHudView@Game@@",8,hud_events},
    {".?AV?$TimeStretchAudioFeeder@M$01@BMSoundLib2017@@",0,audio_methods,Symbol::None,true},
    {".?AV?$TimeStretchAudioFeeder@UF32Stereo@BMSoundLib2017@@M$01@BMSoundLib2017@@",0,audio_methods,Symbol::None,true},
};
struct RttiBindings {
    std::array<std::vector<std::size_t>,symbol_count> methods;
    std::array<std::size_t,symbol_count> tables{};
    unsigned count{};
};
RttiBindings bind_interfaces(const Image& image) {
    // Cache the TypeDescriptor for primary and secondary interfaces of a class.
    std::map<std::string_view,std::vector<std::size_t>> descriptors;
    auto table=[&](const RttiInterface& contract)->std::size_t {
        const std::string_view type=contract.type;
        auto [entry,added]=descriptors.try_emplace(type);
        auto& type_descriptors=entry->second;
        if(added) for(const auto& s:image.sections)
            if((s.flags&IMAGE_SCN_MEM_READ) && !(s.flags&IMAGE_SCN_MEM_EXECUTE) && s.size>=type.size()+17) {
                const auto bytes=image.bytes.subspan(s.start,s.size);
                for(std::size_t i=16;i<=s.size-type.size()-1;++i)
                    if(bytes[i]==std::byte{'.'} && std::memcmp(bytes.data()+i,type.data(),type.size()+1)==0) type_descriptors.push_back(s.start+i-16);
            }
        if(type_descriptors.empty()) return 0;
        if(type_descriptors.size()!=1) throw std::runtime_error(std::string(type)+": RTTI descriptor ambiguous");
        std::vector<std::size_t> tables;
        for(const auto& s:image.sections) if(image.has(s.start,s.size,Access::ReadOnly) && s.size>=24)
            for(std::size_t at=s.start;at<=s.start+s.size-24;at+=4) {
                if(read<std::uint32_t>(image.bytes,at)!=1 || read<std::uint32_t>(image.bytes,at+4)!=contract.subobject ||
                   read<std::uint32_t>(image.bytes,at+8)!=0 || read<std::uint32_t>(image.bytes,at+12)!=type_descriptors[0] ||
                   read<std::uint32_t>(image.bytes,at+20)!=at ||
                   !image.has(read<std::uint32_t>(image.bytes,at+16),16,Access::ReadOnly)) continue;
                for(const auto& t:image.sections) if(image.has(t.start,t.size,Access::ReadOnly) && t.size>=16)
                    for(std::size_t p=t.start;p<=t.start+t.size-16;p+=8)
                        if(read<std::uintptr_t>(image.bytes,p)==image.base+at) tables.push_back(p+8);
            }
        if(tables.size()>1) throw std::runtime_error(std::string(type)+": RTTI vtable ambiguous");
        return tables.empty()?0:tables[0];
    };
    RttiBindings result;
    for(const auto& contract:interfaces) {
        const auto at=table(contract);
        if(!at) {
            if(contract.alternative) continue;
            throw std::runtime_error(std::string(contract.type)+": RTTI interface missing");
        }
        if(contract.table!=Symbol::None) result.tables[static_cast<std::size_t>(contract.table)]=at;
        for(const auto& method:contract.methods) {
            const auto slot=at+method.slot*8;
            if(!image.has(slot,8,Access::ReadOnly)) throw std::runtime_error("RTTI method outside vtable image");
            const auto target=read<std::uintptr_t>(image.bytes,slot);
            if(target<image.base || !image.has(target-image.base,1,Access::Code)) throw std::runtime_error("RTTI method outside executable image");
            auto& targets=result.methods[static_cast<std::size_t>(method.symbol)];
            if(std::find(targets.begin(),targets.end(),target-image.base)==targets.end()) targets.push_back(target-image.base);
        }
    }
    if(result.methods[static_cast<std::size_t>(Symbol::audio_feed)].empty()) throw std::runtime_error("TimeStretchAudioFeeder RTTI interface missing");
    result.count=static_cast<unsigned>(std::count_if(result.methods.begin(),result.methods.end(),[](const auto& x) { return !x.empty(); }));
    return result;
}
std::vector<std::size_t> locate(const Image& image,const Anchor& anchor,const Pattern& pattern,std::span<const std::size_t> seeds={}) {
    std::vector<std::size_t> found;
    if(!seeds.empty()) {
        for(const auto seed:seeds) if(image.has(seed,pattern.bytes.size(),Access::Code) && pattern.matches(image.bytes,seed)) found.push_back(seed);
        if(found.empty()) throw std::runtime_error(std::string(anchor.name)+": RTTI method failed function/ABI verification");
        return found;
    }
    for (const auto& section:image.sections) {
        if (!(section.flags&IMAGE_SCN_MEM_EXECUTE) || !(section.flags&IMAGE_SCN_MEM_READ) ||
            section.flags&IMAGE_SCN_MEM_WRITE || section.size<pattern.bytes.size()) continue;
        const auto data=image.bytes.subspan(section.start,section.size);
        auto begin=data.begin(),end=data.end();
        const auto key=std::span(pattern.bytes).subspan(pattern.key_start,pattern.key_length);
        while (begin!=end) {
            const auto hit=std::search(begin,end,key.begin(),key.end());
            if (hit==end) break;
            const auto index=static_cast<std::size_t>(hit-data.begin());
            if (index>=pattern.key_start) {
                const auto candidate=section.start+index-pattern.key_start;
                if (image.has(candidate,pattern.bytes.size(),Access::Code) && pattern.matches(image.bytes,candidate)) {
                    found.push_back(candidate);
                    if (found.size()>256) throw std::runtime_error(std::string(anchor.name)+": excessive ambiguous candidates");
                }
            }
            begin=hit+1;
        }
    }
    if (found.empty()) throw std::runtime_error(std::string(anchor.name)+": function/ABI signature not found (layout changed or entry patched)");
    return found;
}
std::span<const Family> internal_families() { return catalog; }
bool resolve_family(const Image& image,const RttiBindings& bindings,
                    const Family& family,Resolution& result,std::string& reason) {
    result={};
    try {
        const auto base=image.base;
        if (family.anchors.empty()) throw std::runtime_error("Empty ABI family");
        std::array<std::size_t,symbol_count> symbols{};
        std::vector<std::vector<std::size_t>> candidates;
        std::vector<Pattern> patterns;
        auto assign=[&](Symbol symbol,std::size_t rva) {
            const auto i=static_cast<std::size_t>(symbol);
            if (i>=symbol_count) throw std::runtime_error("Invalid catalog symbol");
            if (symbols[i] && symbols[i]!=rva) throw std::runtime_error(std::string(names[i])+": conflicting references");
            symbols[i]=rva;
        };
        for(std::size_t i=0;i<symbol_count;++i) {
            if(bindings.tables[i]) assign(static_cast<Symbol>(i),bindings.tables[i]);
            if(bindings.methods[i].size()==1) assign(static_cast<Symbol>(i),bindings.methods[i].front());
        }
        for (const auto& anchor:family.anchors) {
            patterns.emplace_back(anchor);
            const auto seeds=anchor.symbol==Symbol::None?std::span<const std::size_t>{}:
                std::span<const std::size_t>(bindings.methods[static_cast<std::size_t>(anchor.symbol)]);
            candidates.push_back(locate(image,anchor,patterns.back(),seeds));
        }
        std::map<unsigned,std::size_t> identities;
        auto inspect=[&](std::size_t i,std::size_t at,bool commit) {
            const auto& anchor=family.anchors[i];
            if (anchor.symbol!=Symbol::None) {
                const auto previous=symbols[static_cast<std::size_t>(anchor.symbol)];
                if (previous && previous!=at) throw std::runtime_error("caller points to a different function");
            }
            std::map<unsigned,std::size_t> local;
            for (const auto& ref:anchor.references) {
                const auto& pattern=patterns[i];
                if (ref.operand>pattern.bytes.size() || pattern.bytes.size()-ref.operand<4 ||
                    ref.next>pattern.bytes.size() || !ref.identity)
                    throw std::runtime_error("Invalid catalog reference");
                for (std::size_t j=0;j<4;++j) if (pattern.fixed[ref.operand+j])
                    throw std::runtime_error("Catalog address operand is not wildcarded");
                const std::int64_t target=ref.kind==ReferenceKind::Relative32?
                    static_cast<std::int64_t>(at+ref.next)+read<std::int32_t>(image.bytes,at+ref.operand):
                    read<std::uint32_t>(image.bytes,at+ref.operand);
                if (target<0 || static_cast<std::uint64_t>(target)>=image.bytes.size())
                    throw std::runtime_error(std::string(anchor.name)+": reference outside module");
                const auto rva=static_cast<std::size_t>(target);
                if (ref.access==Access::Internal) {
                    if (ref.internal>=pattern.bytes.size() || rva!=at+ref.internal)
                        throw std::runtime_error(std::string(anchor.name)+": changed internal dispatch table");
                } else if (!image.has(rva,1,ref.access))
                    throw std::runtime_error(std::string(anchor.name)+": reference section mismatch");
                const auto known=identities.find(ref.identity);
                const auto [entry,inserted]=local.emplace(ref.identity,rva);
                if ((!inserted && entry->second!=rva) || (known!=identities.end() && known->second!=rva))
                    throw std::runtime_error(std::string(anchor.name)+": inconsistent native reference graph");
                if (ref.target!=Symbol::None) {
                    const auto previous=symbols[static_cast<std::size_t>(ref.target)];
                    if (previous && previous!=rva) throw std::runtime_error("native symbol reference mismatch");
                    if (commit) assign(ref.target,rva);
                }
            }
            if (commit) {
                identities.insert(local.begin(),local.end());
                if (anchor.symbol!=Symbol::None) assign(anchor.symbol,at);
            }
        };
        std::vector<bool> committed(family.anchors.size());
        bool changed=true;
        while (changed) {
            changed=false;
            for (std::size_t i=0;i<family.anchors.size();++i) {
                auto& matches=candidates[i];
                std::string rejected;
                std::erase_if(matches,[&](std::size_t at) {
                    try { inspect(i,at,false); return false; }
                    catch (const std::exception& error) { rejected=error.what(); return true; }
                });
                if (matches.empty()) throw std::runtime_error(std::string(family.anchors[i].name)+": "+rejected);
                if (matches.size()==1 && !committed[i]) {
                    inspect(i,matches.front(),true); committed[i]=true; changed=true;
                }
            }
        }
        for (std::size_t i=0;i<candidates.size();++i) if (candidates[i].size()!=1)
            throw std::runtime_error(std::string(family.anchors[i].name)+": ambiguous signature after reference-graph checks");
        Resolution resolved; resolved.family=family.name; resolved.world=family.world;
        resolved.result=family.result; resolved.chart_gauge_gain=family.chart_gauge_gain; resolved.interface_methods=bindings.count;
        for (std::size_t i=0;i<patterns.size();++i) for (const auto& variant:patterns[i].variants) {
            const auto begin=image.bytes.begin()+candidates[i].front()+variant.offset;
            if (std::equal(variant.replacement.begin(),variant.replacement.end(),begin))
                resolved.details.push_back("Compatible runtime patch: "+variant.name);
        }
        for(std::size_t i=0;i<patterns.size();++i) for(const auto& selector:patterns[i].selectors) {
            const auto maximum=read<std::uint32_t>(image.bytes,candidates[i].front()+selector.maximum);
            const auto fallback=read<std::uint32_t>(image.bytes,candidates[i].front()+selector.fallback);
            if(maximum!=read<std::uint32_t>(patterns[i].bytes,selector.maximum) || fallback!=read<std::uint32_t>(patterns[i].bytes,selector.fallback))
                resolved.details.push_back("Compatible content selector: maximum="+std::to_string(maximum)+", fallback="+std::to_string(fallback));
        }
        std::array<std::uintptr_t,symbol_count> values{};
        std::size_t verified{};
        for (std::size_t i=0;i<symbol_count;++i) {
            const auto rva=symbols[i];
            const auto symbol=static_cast<Symbol>(i);
            const bool absent=(symbol==Symbol::gauge_chart_options && !family.chart_gauge_gain) ||
                (symbol==Symbol::timed_stage_limit && family.result.fixed_timed_limit!=0);
            if(absent) {
                if(rva) throw std::runtime_error("Unexpected interface in legacy ABI family");
                continue;
            }
            const auto access=kinds[i]==Kind::Code?Access::Code:kinds[i]==Kind::Writable?Access::Writable:Access::ReadOnly;
            const auto slots=kinds[i]==Kind::Vtable?3u:kinds[i]==Kind::Vtable2?2u:0u;
            const auto size=slots?slots*8u:(static_cast<Symbol>(i)==Symbol::matching || static_cast<Symbol>(i)==Symbol::player_settings)?8u:4u;
            if (!rva || !image.has(rva,size,access)) throw std::runtime_error(std::string(names[i])+": unresolved or invalid symbol");
            if (slots) {
                if (rva%8) throw std::runtime_error(std::string(names[i])+": unaligned vtable");
                for (unsigned j=0;j<slots;++j) {
                    const auto function=read<std::uintptr_t>(image.bytes,rva+j*8);
                    if (function<base || !image.has(function-base,1,Access::Code))
                        throw std::runtime_error(std::string(names[i])+": invalid vtable slot");
                }
            }
            values[i]=rva;
            ++verified;
            std::ostringstream line; line<<names[i]<<" = 0x"<<std::hex<<rva;
            resolved.details.push_back(line.str());
        }
        std::size_t index{};
#define TM_SYMBOL(name, kind) resolved.addresses.name=values[index++];
#include "native/profile_symbols.inc"
#undef TM_SYMBOL
        reason=resolved.family+": "+std::to_string(family.anchors.size())+" function/ABI guards and "+
            std::to_string(verified)+" dynamic symbols verified";
        result=std::move(resolved); return true;
    } catch (const std::exception& error) { reason=error.what(); return false; }
}
}
std::span<const Family> families() { return internal_families(); }
std::string_view symbol_name(Symbol symbol) {
    const auto i=static_cast<std::size_t>(symbol); return i<symbol_count?names[i]:"none";
}
bool resolve_image(std::span<const std::byte> bytes,std::uintptr_t base,const Family& family,Resolution& result,std::string& reason) {
    result={};
    try { const Image image(bytes,base); const auto bindings=bind_interfaces(image); return resolve_family(image,bindings,family,result,reason); }
    catch(const std::exception& error) { reason=error.what(); return false; }
}
bool resolve_image(std::span<const std::byte> bytes,std::uintptr_t base,Resolution& result,std::string& reason) {
    result={};
    try {
        const Image image(bytes,base); const auto bindings=bind_interfaces(image);
        Resolution resolved; std::string failures,success;
        for(const auto& family:catalog) {
            Resolution candidate; std::string detail;
            if(resolve_family(image,bindings,family,candidate,detail)) {
                if(!resolved.family.empty()) throw std::runtime_error("Ambiguous native ABI families; no game hooks installed");
                resolved=std::move(candidate); success=std::move(detail);
            } else failures+=std::string(family.name)+": "+detail+"; ";
        }
        if(resolved.family.empty()) throw std::runtime_error("No compatible native ABI. No game hooks installed. "+failures);
        result=std::move(resolved); reason=std::move(success); return true;
    } catch(const std::exception& error) { reason=error.what(); return false; }
}
}
