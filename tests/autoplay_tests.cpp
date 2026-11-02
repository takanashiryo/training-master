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
void check(bool ok,const char* message);
training::AutoplayMode requested_mode{};
bool automatic_request{};
unsigned char native_before{};
bool __fastcall native_gate(Ptr,Ptr) { return false; }
Ptr __fastcall points(Ptr object) { return *reinterpret_cast<Ptr*>(object+8); }
void __fastcall mixed_update(Ptr) {
    check(*flag==native_before,"partial mode must retain manual input sampling at frame start");
    alignas(8) std::array<Ptr,3> table{0,0,reinterpret_cast<Ptr>(points)};
    alignas(8) std::array<unsigned char,48> point{};
    std::array<Ptr,2> bounds{reinterpret_cast<Ptr>(point.data()),reinterpret_cast<Ptr>(point.data()+48)};
    std::array<Ptr,3> object{0,reinterpret_cast<Ptr>(table.data()),reinterpret_cast<Ptr>(bounds.data())};
    Ptr note=reinterpret_cast<Ptr>(object.data());
    // Alternating BT/FX chips, holds, left/right VOL and unknown notes.
    for(unsigned family:{1u,2u,1u,2u,0u,1u}) {
        point[36]=static_cast<unsigned char>(family);
        check(!autoplay::note_gate(native_gate,0,reinterpret_cast<Ptr>(&note)),"native completion result passes through");
        const bool selected=automatic_request && ((requested_mode==AutoplayMode::Buttons && family==1) ||
            (requested_mode==AutoplayMode::Knobs && family==2));
        check(*flag==(selected || native_before),"only the selected note family uses automatic judgment and follow");
    }
}
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
void __fastcall throwing_partial_update(Ptr world) { mixed_update(world); RaiseException(0xe0421234,0,0,nullptr); }
bool partial_exception_restores(Ptr world) {
    __try { autoplay::update(world,true,throwing_partial_update,requested_mode); }
    __except(GetExceptionCode()==0xe0421234?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
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
        layout.note_gate=reinterpret_cast<Ptr>(native_gate); autoplay::configure(layout);
        for(auto mode:{AutoplayMode::Buttons,AutoplayMode::Knobs}) for(bool enabled:{false,true}) for(unsigned before:{0u,1u}) {
            requested_mode=mode; automatic_request=enabled; native_before=static_cast<unsigned char>(before); value=native_before;
            autoplay::update(address,enabled,mixed_update,mode);
            check(value==native_before,"partial mode restores the shared native flag after the frame");
        }
        value=0; autoplay::note_gate(native_gate,0,0);
        check(value==0,"note gate outside an owned update cannot force AutoPlay");
        requested_mode=AutoplayMode::Buttons; automatic_request=true; native_before=0;
        check(partial_exception_restores(address) && value==0,"partial mode must restore flags even on Windows SEH");
        autoplay::note_gate(native_gate,0,1);
        check(value==0,"SEH also restores the thread's selective-note context");
        autoplay::clear_counters();
        value=0; check(exception_restores(address) && value==0 && observed,"Windows SEH must restore native autoplay flag");
        value=2; autoplay::update(address,true,native_update);
        check(value==2 && autoplay::snapshot().faulted && autoplay::snapshot().faults==1,"invalid flag stops override without writing");
        value=0; autoplay::begin_track(); autoplay::update(address,true,native_update);
        check(observed && value==0 && !autoplay::snapshot().faulted,"next track can recover with a valid native context");
        autoplay::clear_counters(); const auto cleared=autoplay::snapshot();
        check(cleared.updates==0 && cleared.overrides==0 && cleared.judgments[6]==0,"Clear counters resets diagnostics");
        std::cout<<"44 native judgment/flag combinations, mixed button/VOL modes, SEH restoration and fault recovery passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
