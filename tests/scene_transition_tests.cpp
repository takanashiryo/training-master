#include "game/scene_transition.hpp"
#include "native/profile.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
// Replay the native leaf request AND CAppMainFlow's real transition decision.
// Resource loading remains a substitute; DllMain and game imports stay disabled.
namespace {
using Ptr=std::uintptr_t;
struct Request { int layer,padding; Ptr argument; int kind,parent,next,source; };
struct Event { unsigned process; int action; Request request; };
struct Info { const Ptr* vtable; unsigned id; };
struct Manager {
    const Ptr* vtable;
    Ptr image{};
    alignas(8) std::array<std::byte,128> flow{};
    std::vector<Event> events;
    unsigned entered{};
    int reject_action{};
    int& current() { return *reinterpret_cast<int*>(flow.data()+88); }
};
struct Process {
    const Ptr* vtable;
    std::array<Ptr,8> resources{};
    std::array<int,5>* state;
    Info* info;
    Manager* manager;
};
static_assert(offsetof(Process,state)==72 && offsetof(Request,kind)==16);
unsigned __fastcall get_id(Info* self) { return self->id; }
Info* __fastcall get_info(Process* self) { return self->info; }
Manager* __fastcall get_manager(Process* self) { return self->manager; }
bool __fastcall invoke(Manager* self,unsigned process,int action,Ptr argument) {
    Event event{process,action,{}};
    if (action==1) std::memcpy(&event.request,reinterpret_cast<void*>(argument),sizeof(Request));
    self->events.push_back(event);
    if (action==self->reject_action) return false;
    if (action==1) {
        if (event.request.kind==3 && process==2) {
            using Decide=unsigned(__fastcall*)(Ptr,int,Ptr);
            // The decision invoked by CAppMainFlow::OnProcess, not a test copy
            // of its state table. OnProcess stores this result as current state.
            const auto target=reinterpret_cast<Decide>(self->image+training::native::addresses().flow_decision)(
                reinterpret_cast<Ptr>(self->flow.data()),self->current(),event.request.argument);
            self->current()=static_cast<int>(target);
            self->entered=target;
        } else self->entered=process;
    }
    return true;
}
void check(bool c,const char* message) { if (!c) throw std::runtime_error(message); }
}
int wmain(int argc,wchar_t** argv) {
    if (argc!=2) return 2;
    const auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if (!module) return 2;
    int result=0;
    try {
        std::string reason;
        const bool compatible=training::native::validate(module,reason); check(compatible,reason.c_str());
        const Ptr image=reinterpret_cast<Ptr>(module);
        const auto next=reinterpret_cast<training::NativeNext>(image+training::native::addresses().request_next);
        const std::array<Ptr,1> info_vtable{reinterpret_cast<Ptr>(get_id)};
        const std::array<Ptr,1> manager_vtable{reinterpret_cast<Ptr>(invoke)};
        std::array<Ptr,18> vtable{};
        vtable[4]=reinterpret_cast<Ptr>(get_info); vtable[6]=reinterpret_cast<Ptr>(get_manager);
        Manager manager{manager_vtable.data(),image};
        // Native IDs: APPMAINFLOW=2, MUSICSELECT=18, GAME=43, RESULT=16.
        std::array<int,5> state{2,0x7fffffff,0x7fffffff,1,1};
        Info game_info{info_vtable.data(),43},select_info{info_vtable.data(),18};
        Process game{vtable.data(),{},&state,&game_info,&manager};
        Process select{vtable.data(),{},&state,&select_info,&manager};
        const auto route=training::capture_music_select_route(reinterpret_cast<Ptr>(&select));
        check(route.valid() && route.process_id==18,"capture selection route with sentinel default successor");
        for (int repeat=0;repeat<3;++repeat) {
            manager.current()=43; manager.events.clear();
            check(training::request_music_select(reinterpret_cast<Ptr>(&game),route,next),"practice exit rejected");
            check(manager.entered==18 && manager.current()==18,
                  "selection must advance APPMAINFLOW; sibling-only jump leaves it in gameplay");
            check(manager.events.back().request.kind==3 && manager.events.back().request.argument==18,
                  "must use native no-result return-to-selection argument");
            manager.events.clear();
            const Ptr selected_chart=0x1234;
            check(next(reinterpret_cast<Ptr>(&select),selected_chart),"next song request rejected");
            check(manager.entered==43 && manager.current()==43,
                  "next selection would enter RESULT_SCENE with no result and crash");
            check(manager.events.back().request.argument==selected_chart,"preserve next selected chart argument");
        }
        manager.events.clear(); manager.reject_action=3;
        check(!training::request_music_select(reinterpret_cast<Ptr>(&game),route,next) && manager.events.size()==1,
              "detach rejection must not resume parent");
        manager.events.clear(); manager.reject_action=0;
        state[0]=8;
        check(!training::request_music_select(reinterpret_cast<Ptr>(&game),route,next) && manager.events.empty(),"reject stale parent");
        state[0]=2; state[3]=2;
        check(!training::request_music_select(reinterpret_cast<Ptr>(&game),route,next) && manager.events.empty(),"reject stale layer");
        std::cout<<"Native parent flow: GAME -> MUSICSELECT -> GAME passed for three consecutive practices.\n";
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; result=1; }
    FreeLibrary(module); return result;
}
