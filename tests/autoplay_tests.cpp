#include "game/autoplay.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
using namespace training;
using Ptr=std::uintptr_t;
std::array<unsigned char,40> expected{},actual{};
unsigned char* flag{};
bool observed{};
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
Ptr __fastcall native_judge(Ptr,Ptr output,Ptr,float) {
    std::memcpy(reinterpret_cast<void*>(output),expected.data(),expected.size()); return output;
}
void __fastcall native_update(Ptr) {
    observed=*flag==1;
    check(autoplay::judge(native_judge,0,reinterpret_cast<Ptr>(actual.data()),0,.5f)==reinterpret_cast<Ptr>(actual.data()),
          "native judge return pointer must pass through");
}
void __fastcall throwing_update(Ptr) { observed=*flag==1; RaiseException(0xe0421234,0,0,nullptr); }
bool exception_restores(Ptr world) {
    __try { autoplay::update(world,true,throwing_update); }
    __except(GetExceptionCode()==0xe0421234?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
}
int main() {
    try {
        alignas(8) std::array<unsigned char,0x400> world{},context{};
        autoplay_discovery::Layout layout;
        layout.context=0x1d8; layout.flags=0x68;
        layout.world_vtable=0x12345678; layout.context_vtable=0x456789ab;
        auto put=[](auto& bytes,std::size_t at,Ptr value) { std::memcpy(bytes.data()+at,&value,sizeof(value)); };
        put(world,0,layout.world_vtable); put(context,0,layout.context_vtable);
        put(world,layout.context,reinterpret_cast<Ptr>(context.data()));
        unsigned char value{}; flag=&value;
        put(context,layout.flags,reinterpret_cast<Ptr>(flag));
        const auto address=reinterpret_cast<Ptr>(world.data());
        autoplay::configure(layout);
        unsigned cases{};
        for (bool automatic:{false,true}) for (unsigned before:{0u,1u}) for (unsigned id=0;id<=10;++id) {
            expected.fill(0xa5); std::memcpy(expected.data(),&id,sizeof(id));
            const float delta=.008f; std::memcpy(expected.data()+8,&delta,sizeof(delta));
            value=static_cast<unsigned char>(before); observed=false;
            autoplay::update(address,automatic,native_update);
            check(actual==expected,"all native judgment bytes (including S-CRITICAL option) must remain unchanged");
            check(value==before && observed==(automatic || before),"native flag must be scoped to update and restored");
            ++cases;
        }
        const auto stats=autoplay::snapshot();
        check(cases==44 && stats.updates==44 && stats.overrides==22 && !stats.faults,"update diagnostics");
        for (unsigned id=1;id<=10;++id) check(stats.judgments[id]==4,"native judgment counts");
        check(stats.last_error>.0079f && stats.last_error<.0081f,"timing delta must be observed without rewriting");
        value=0; check(exception_restores(address) && value==0 && observed,"Windows SEH must restore native autoplay flag");
        value=2; autoplay::update(address,true,native_update);
        check(value==2 && autoplay::snapshot().faulted && autoplay::snapshot().faults==1,"invalid flag stops override without writing");
        value=0; autoplay::begin_track(); autoplay::update(address,true,native_update);
        check(observed && value==0 && !autoplay::snapshot().faulted,"next track can recover with a valid native context");
        autoplay::clear_counters(); const auto cleared=autoplay::snapshot();
        check(cleared.updates==0 && cleared.overrides==0 && cleared.judgments[6]==0,"Clear counters resets diagnostics");
        std::cout<<"44 native judgment/flag combinations, SEH restoration and fault recovery passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
