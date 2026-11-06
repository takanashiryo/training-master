#include "game/practice_stage.hpp"
#include "native_fixture.hpp"
#include "native/profile.hpp"
#include "game/game_policy.hpp"
#include <MinHook.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <vector>
// Isolated native replay: DllMain/import initialization remain disabled. Only
// allocator, RTTI lookup, record conversion and XML output are test substitutes.
// Native Append, container copies/growth/destruction and serializer selection
// execute from the exact game binary. Nothing is written to a game/server.
namespace {
using Ptr=std::uintptr_t;
template<class T> T& at(Ptr p,std::size_t o) { return *reinterpret_cast<T*>(p+o); }
struct Data { Ptr vtable{},begin{},end{},capacity{}; int count{}; int padding{}; };
Data* active{};
std::unordered_set<void*> allocations;
std::vector<unsigned> converted;
unsigned tracks{};
int decided_state{};
unsigned native_matching_finishes{};
void __fastcall matching_finish(Ptr) { ++native_matching_finishes; }
Ptr decided_argument{};
std::int64_t clock_value{};
std::int64_t __fastcall clock_frequency() { return 1'000'000'000; }
std::int64_t __fastcall clock_now() { return clock_value; }
unsigned __fastcall stage_limit(int) { return 3; }
unsigned __fastcall timed_limit() { return 4; }
bool __fastcall continue_override() { return false; }
unsigned __fastcall flow_probe(Ptr,int state,Ptr argument) {
    decided_state=state; decided_argument=argument; return state==16?17u:18u;
}
void* __fastcall allocate(std::size_t size) {
    auto* p=std::malloc(size); if (!p) std::abort(); allocations.insert(p); return p;
}
void __fastcall release(void* p) { if (p) { allocations.erase(p); std::free(p); } }
Ptr __fastcall rtti() { return reinterpret_cast<Ptr>(active); }
Ptr __fastcall convert(Ptr output,Ptr record) {
    converted.push_back(at<unsigned>(record,training::native::result_layout().field(1708)));
    std::memset(reinterpret_cast<void*>(output),0,2288);
    return output;
}
Ptr __fastcall xml(Ptr,Ptr,int,const char* name,...) { if (!std::strcmp(name,"track")) ++tracks; return 0; }
void check(bool c,const char* message) { if (!c) throw std::runtime_error(message); }
void hook(Ptr image,Ptr offset,void* replacement) {
    void* unused{};
    check(MH_CreateHook(reinterpret_cast<void*>(image+offset),replacement,&unused)==MH_OK,"native replay hook creation");
    check(MH_EnableHook(reinterpret_cast<void*>(image+offset))==MH_OK,"native replay hook activation");
}
}
int wmain(int argc,wchar_t** argv) {
    if (argc!=2) return 2;
    const auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if (!module) return 2;
    const Ptr image=reinterpret_cast<Ptr>(module);
    std::string reason;
    if (!training::native::validate(module,reason)) { std::cerr<<reason<<'\n'; FreeLibrary(module); return 2; }
    const int fixture=native_fixture::select(reason);
    if(fixture<0) { FreeLibrary(module); return 77; }
    const auto rva=[&](Ptr old) { return native_fixture::rva(fixture,old); };
    int result=0;
    Ptr original_xml{};
    try {
        check(MH_Initialize()==MH_OK,"MinHook init");
        hook(image,rva(0x7accd4),reinterpret_cast<void*>(allocate));
        hook(image,rva(0x751600),reinterpret_cast<void*>(release));
        hook(image,rva(0x7dc7c0),reinterpret_cast<void*>(rtti));
        hook(image,rva(0x5d1460),reinterpret_cast<void*>(convert));
        hook(image,training::native::addresses().stage_limit,reinterpret_cast<void*>(stage_limit));
        if(training::native::addresses().timed_stage_limit) hook(image,training::native::addresses().timed_stage_limit,reinterpret_cast<void*>(timed_limit));
        hook(image,rva(0x5d36e0),reinterpret_cast<void*>(continue_override));
        hook(image,rva(0x79f83c),reinterpret_cast<void*>(clock_frequency));
        hook(image,rva(0x79f820),reinterpret_cast<void*>(clock_now));
        at<int>(image,training::native::addresses().game_mode)=3;
        DWORD old{};
        check(VirtualProtect(reinterpret_cast<void*>(image+rva(0x884ef8)),sizeof(Ptr),PAGE_READWRITE,&old)!=0,"XML test IAT access");
        original_xml=at<Ptr>(image,rva(0x884ef8));
        at<Ptr>(image,rva(0x884ef8))=reinterpret_cast<Ptr>(xml);
        DWORD unused{}; VirtualProtect(reinterpret_cast<void*>(image+rva(0x884ef8)),sizeof(Ptr),old,&unused);
        Data data; active=&data;
        data.vtable=image+training::native::addresses().stage_result_vtable;
        if(training::native::addresses().stage_data_slot)
            at<Ptr>(image,training::native::addresses().stage_data_slot)=reinterpret_cast<Ptr>(&data);
        check(training::native::stage_result(image)==reinterpret_cast<Ptr>(&data),"native stage object access");
        data.vtable=0;
        check(!training::native::stage_result(image),"reject unexpected stage object type");
        data.vtable=image+training::native::addresses().stage_result_vtable;
        const auto serialize=reinterpret_cast<training::NativeSerialize>(image+training::native::addresses().serialize_result);
        const auto native_continue=reinterpret_cast<training::NativeContinue>(image+training::native::addresses().can_continue);
        const auto can_continue=[&] { return training::can_continue_practice_safe(image,reinterpret_cast<Ptr>(&data),native_continue); };
        const auto save=[&](int play_id) {
            at<int>(image,training::native::addresses().play_id)=play_id;
            converted.clear(); tracks=0;
            check(training::serialize_practice_safe(image,reinterpret_cast<Ptr>(&data),0,serialize)==1,"serialize result");
        };
        std::array<std::byte,236> options{};
        {
            training::PracticeStageRecord record(0,1234,3,options);
            check(training::append_practice_stage(image,reinterpret_cast<Ptr>(&data),record),"practice must consume one stage");
        } // Borrowed template dies here: native copy must own all its containers.
        check(data.count==1 && data.end-data.begin==training::stage_stride(),"count and native vector must advance together");
        check(can_continue(),"after first practice the next paid stage must be available");
        check(training::decide_practice_flow(image,0,43,18,native_continue,flow_probe)==18 && decided_state==43,
              "first practice must return selection through native parent flow");
        save(1);
        check(converted.empty() && tracks==0,"pending-track branch must exclude practice");
        save(0);
        check(converted.empty() && tracks==0,"play_id=0 fallback must not serialize the last practice slot");
        // A later ordinary result remains eligible, with its true stage number.
        {
            training::PracticeStageRecord record(1,5678,2,options);
            at<unsigned>(record.address(),training::native::result_layout().tag)=0;
            at<unsigned>(record.address(),training::native::result_layout().field(248))=2;
            at<std::uint8_t>(record.address(),training::native::result_layout().excluded)=0;
            using Append=void(__fastcall*)(Ptr,Ptr);
            reinterpret_cast<Append>(image+training::native::addresses().append_result)(reinterpret_cast<Ptr>(&data),record.address());
        }
        save(1); check(converted==std::vector<unsigned>{5678} && tracks==1,"mixed save must include only ordinary track");
        save(0); check(converted==std::vector<unsigned>{5678} && tracks==1,"ordinary last-track fallback still saves");
        check(can_continue(),"ordinary last result must use native continuation logic");
        // Grow past the native allocator's 4096-byte aligned allocation path.
        for (unsigned i=2;i<10;++i) {
            training::PracticeStageRecord record(i,2000+i,1,options);
            check(training::append_practice_stage(image,reinterpret_cast<Ptr>(&data),record),"repeated stage append");
            if (i==2) {
                check(!can_continue(),"third practice must exhaust a three-stage budget");
                check(training::decide_practice_flow(image,0,43,18,native_continue,flow_probe)==17 && decided_state==16 && decided_argument==0,
                      "last practice must use native post-result routing, not reopen selection indefinitely");
            }
        }
        check(data.count==10 && data.end-data.begin==10*training::stage_stride(),"reallocation preserves stage-count invariant");
        for (unsigned i=0;i<10;++i) {
            const auto p=data.begin+i*training::stage_stride();
            check(at<unsigned>(p,training::native::result_layout().field(1700))==i,"native growth preserves ordinal");
            check((i==1)==!training::is_practice_stage(p),"native copies preserve private tag");
        }
        save(1); check(converted==std::vector<unsigned>{5678} && tracks==1,"growth must not expose practice to save");
        save(0); check(converted.empty() && tracks==0,"last-practice fallback after mixed history");
        at<std::uint8_t>(data.begin,training::native::result_layout().excluded)=0;
        save(1); check(converted==std::vector<unsigned>{5678} && tracks==1,"save guard must restore practice exclusion flag");
        // Premium Time is game mode 6, independent of course options 1/18.
        // Execute the real native stopwatch branch with a deterministic QPC.
        at<int>(image,training::native::addresses().game_mode)=6;
        alignas(8) std::array<std::byte,1664> world{};
        const Ptr world_address=reinterpret_cast<Ptr>(world.data());
        at<Ptr>(world_address,0)=image+training::native::addresses().world_vtable;
        // The native selection UI reads PlayerSetting +44/+48 after scene recreation.
        // Emulate a prior selection and a different training song using the exact
        // resolved global and the game's validated primary vtable.
        alignas(8) std::array<std::byte,128> player_settings{};
        const Ptr setting_address=reinterpret_cast<Ptr>(player_settings.data());
        const Ptr setting_global=image+training::native::addresses().player_settings;
        const Ptr old_setting=at<Ptr>(setting_global,0);
        at<Ptr>(setting_global,0)=setting_address;
        at<Ptr>(setting_address,0)=image+training::native::addresses().player_settings_vtable;
        at<unsigned>(setting_address,44)=100; at<unsigned>(setting_address,48)=1;
        at<unsigned>(world_address,training::native::world_layout().music)=5678;
        at<unsigned>(world_address,training::native::world_layout().music+4)=4;
        check(training::remember_practice_selection(image,world_address),"training exit must update native selection memory");
        check(at<unsigned>(setting_address,44)==5678 && at<unsigned>(setting_address,48)==4,
              "returning to song selection must retain the trained music and difficulty");
        at<unsigned>(world_address,training::native::world_layout().music+4)=99;
        check(training::remember_practice_selection(image,world_address) && at<unsigned>(setting_address,48)==0,
              "invalid difficulty must use the native fallback");
        at<Ptr>(setting_address,0)=0;
        check(!training::remember_practice_selection(image,world_address) && at<unsigned>(setting_address,44)==5678,
              "unexpected settings object must not be modified");
        at<Ptr>(setting_global,0)=old_setting;
        at<std::uint8_t>(world_address,training::native::world_layout().demo+12)=at<std::uint8_t>(world_address,training::native::world_layout().demo+13)=at<std::uint8_t>(world_address,training::native::world_layout().no_fail)=1;
        check(training::supports_training_world(image,world_address),"Premium Time world flags must allow training");
        at<int>(image,training::native::addresses().game_mode)=0;
        check(!training::supports_training_world(image,world_address),"Normal must still reject unsupported no-fail worlds");
        at<std::uint8_t>(world_address,training::native::world_layout().no_fail)=0;
        check(training::supports_training_world(image,world_address),"Normal support must remain available");
        for(int mode=-1;mode<=12;++mode) {
            at<int>(image,training::native::addresses().game_mode)=mode;
            at<std::uint8_t>(world_address,training::native::world_layout().no_fail)=mode==6;
            const bool allowed=mode==0 || mode==6;
            check(training::supports_training_world(image,world_address)==allowed,"native mode matrix permits only Normal and Premium Time");
            check((training::training_world_access(image,world_address)==training::TrainingAccess::Allowed)==allowed,
                  "native mode warning eligibility must match the backend gate");
        }
        at<int>(image,training::native::addresses().game_mode)=6;
        at<std::uint8_t>(world_address,training::native::world_layout().no_fail)=1;
        for (const auto offset:{training::native::world_layout().demo,training::native::world_layout().special,training::native::world_layout().battle}) {
            at<std::uint8_t>(world_address,offset)=1;
            check(!training::supports_training_world(image,world_address),"Premium Time must still reject demo/tutorial/matching worlds");
            if(offset==training::native::world_layout().demo)
                check(training::training_world_access(image,world_address)==training::TrainingAccess::Demo,"DEMO ATTRACTION worlds must be rejected before track binding");
            at<std::uint8_t>(world_address,offset)=0;
        }
        at<Ptr>(world_address,0)+=8;
        check(training::training_world_access(image,world_address)==training::TrainingAccess::Demo &&
              !training::supports_training_world(image,world_address),"title-demo world subclasses must not bind as a player world");
        at<Ptr>(world_address,0)-=8;
        alignas(8) std::array<std::byte,640> matching{};
        at<int>(reinterpret_cast<Ptr>(matching.data()),632)=1;
        const auto previous_matching=at<Ptr>(image,rva(0x1261bd0));
        at<Ptr>(image,rva(0x1261bd0))=reinterpret_cast<Ptr>(matching.data());
        check(!training::supports_training_world(image,world_address),"Premium Time matchmaking remains unsupported");
        at<Ptr>(image,rva(0x1261bd0))=previous_matching;
        check(training::prepare_training_finish(image,world_address),"Premium Time quiet finish preparation");
        check(at<std::uint8_t>(world_address,training::native::world_layout().forced_end)==1,"Finish must bypass Premium Time native auto-retry");
        at<std::uint8_t>(world_address,training::native::world_layout().forced_end)=0;
        at<int>(image,training::native::addresses().game_mode)=3;
        check(training::prepare_training_finish(image,world_address),"Normal quiet finish preparation");
        check(at<std::uint8_t>(world_address,training::native::world_layout().forced_end)==1,"Normal Finish must force native exit even while a completion announcement is pending");
        // Execute the real World Update + final cleanup state in each mode.
        // Clock boundaries are already substituted above; no presentation state,
        // native audio API, HUD resources or game DllMain is invoked.
        {
            const auto& a=training::native::addresses();
            const training::native::Anchor* exit_anchor{};
            for(const auto& family:training::native::families()) if(family.name==training::native::resolution().family)
                for(const auto& anchor:family.anchors) if(anchor.symbol==training::native::Symbol::quiet_exit_state) exit_anchor=&anchor;
            check(exit_anchor!=nullptr,"quiet exit must have a full ABI guard");
            const auto reference=exit_anchor->references.front();
            const Ptr global=image+a.quiet_exit_state+reference.next+at<std::int32_t>(image+a.quiet_exit_state,reference.operand);
            const Ptr previous=at<Ptr>(global,0);
            alignas(8) std::array<std::byte,4096> descriptor{};
            at<Ptr>(global,0)=reinterpret_cast<Ptr>(descriptor.data());
            try {
                std::array<Ptr,32> matching_table{};
                const auto matching_slot=at<unsigned char>(image+a.quiet_exit_state,179);
                check(matching_slot%sizeof(Ptr)==0 && matching_slot/sizeof(Ptr)<matching_table.size(),
                      "reviewed native matching notification virtual slot");
                matching_table[matching_slot/sizeof(Ptr)]=reinterpret_cast<Ptr>(matching_finish);
                Ptr matching_object=reinterpret_cast<Ptr>(matching_table.data());
                const auto matching_offset=at<unsigned>(image+a.quiet_exit_state,164);
                check(matching_offset<1024,"reviewed native matching member offset");
                using WorldUpdate=char(__fastcall*)(Ptr);
                const auto update=reinterpret_cast<WorldUpdate>(image+a.world_update);
                for(int mode:{0,1,3,6}) {
                    at<int>(image,a.game_mode)=mode;
                    alignas(8) std::array<std::byte,2048> quiet_world{};
                    const Ptr q=reinterpret_cast<Ptr>(quiet_world.data());
                    at<Ptr>(q,0)=image+a.world_vtable; at<Ptr>(q,16)=q;
                    at<Ptr>(q,24)=image+a.play_state; at<unsigned char>(q,108)=1;
                    at<Ptr>(q,matching_offset)=reinterpret_cast<Ptr>(&matching_object);
                    native_matching_finishes=0;
                    check(training::prepare_training_finish(image,q),"quiet finish must queue native cleanup");
                    for(int frame=0;frame<5 && !at<unsigned char>(q,training::native::world_layout().forced_end-1);++frame) {
                        clock_value+=16'666'667;
                        update(q);
                        check(at<Ptr>(q,24)==image+a.quiet_exit_state,"Finish must never enter play/end presentation during teardown");
                    }
                    check(at<unsigned char>(q,training::native::world_layout().forced_end-1)==1 && native_matching_finishes==1,
                          "quiet finish must complete native matching teardown and signal the scene to close");
                    check(!at<unsigned char>(q,training::native::world_layout().demo) && !at<unsigned char>(q,training::native::world_layout().special),
                          "quiet finish must not impersonate a demo or alter native mode flags");
                }
            } catch(...) { at<Ptr>(global,0)=previous; throw; }
            at<Ptr>(global,0)=previous;
        }
        at<int>(image,training::native::addresses().game_mode)=6;
        at<unsigned>(image,rva(0x1277e80))=1;
        const Ptr premium=image+rva(0x1277e88);
        std::memset(reinterpret_cast<void*>(premium),0,40);
        at<std::int64_t>(premium,24)=100;
        at<int>(premium,32)=2;
        clock_value=50;
        check(can_continue(),"Premium Time with time left must continue even after ten tracks");
        check(training::decide_practice_flow(image,0,43,18,native_continue,flow_probe)==18 && decided_state==43,
              "Premium Time must return selection while budget remains");
        clock_value=101;
        check(!can_continue(),"Premium Time expiry must end regardless of track count");
        check(training::decide_practice_flow(image,0,43,18,native_continue,flow_probe)==17 && decided_state==16,
              "Premium Time expiry must resume native session-end routing");
        clock_value=100;
        check(can_continue(),"preserve native exact-deadline comparison");
        at<int>(premium,32)=1;
        at<std::int64_t>(premium,8)=50;
        clock_value=1000;
        check(can_continue(),"native suspended budget must use its captured timestamp");
        at<int>(premium,32)=0;
        at<std::int64_t>(premium,24)=0x7fffffffffffffff;
        check(can_continue(),"preserve native unlimited-clock sentinel");
        at<std::int64_t>(premium,24)=0;
        check(!can_continue(),"uninitialized/empty Premium Time budget must not become normal stages");
        save(1); check(converted==std::vector<unsigned>{5678} && tracks==1,"Premium Time batch save must still exclude practice");
        save(0); check(converted.empty() && tracks==0,"Premium Time last-track save must exclude practice");
        at<int>(image,training::native::addresses().game_mode)=3;
        using Reset=void(__fastcall*)(Ptr);
        reinterpret_cast<Reset>(image+rva(0x341ed0))(reinterpret_cast<Ptr>(&data));
        check(data.count==0 && data.end==data.begin,"native new-credit reset");
        save(0); check(converted.empty() && tracks==0,"empty post-reset save must not underflow");
        options[93]=std::byte{1};
        for (unsigned i=0;i<4;++i) {
            training::PracticeStageRecord record(i,3000+i,1,options);
            check(training::append_practice_stage(image,reinterpret_cast<Ptr>(&data),record),"timed-mode stage append");
            const auto limit=training::native::result_layout().fixed_timed_limit?training::native::result_layout().fixed_timed_limit:4u;
            check(can_continue()==(i+1<limit),"timed mode must use its own native stage budget");
        }
        reinterpret_cast<Reset>(image+rva(0x341ed0))(reinterpret_cast<Ptr>(&data));
        void* storage=reinterpret_cast<void*>(data.begin);
        if (data.capacity-data.begin>=4096) storage=reinterpret_cast<void**>(storage)[-1];
        release(storage);
        check(allocations.empty(),"native copies, growth and reset must not leak");
        std::cout<<"Native stage consumption and both serializer branches passed; next ordinary result preserved.\n";
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; result=1; }
    if (original_xml) {
        DWORD old{},unused{}; VirtualProtect(reinterpret_cast<void*>(image+rva(0x884ef8)),sizeof(Ptr),PAGE_READWRITE,&old);
        at<Ptr>(image,rva(0x884ef8))=original_xml;
        VirtualProtect(reinterpret_cast<void*>(image+rva(0x884ef8)),sizeof(Ptr),old,&unused);
    }
    MH_Uninitialize(); FreeLibrary(module);
    return result;
}
