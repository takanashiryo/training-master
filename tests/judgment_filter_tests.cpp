#include "game/judgment_filter.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace training::judgment_filter;
namespace {
void check(bool b,const char* m) { if(!b) throw std::runtime_error(m); }
unsigned calls{}; bool all_failed{};
Ptr __fastcall original(Ptr,Ptr output,Ptr,Ptr,float) { ++calls; *reinterpret_cast<unsigned char*>(output)=7; return output; }
bool __fastcall failed(Ptr) { return all_failed; }
struct Bounds { Ptr begin,end; };
Bounds bounds{};
Ptr __fastcall points(Ptr) { return reinterpret_cast<Ptr>(&bounds); }
template<class T> void put(void* p,std::size_t n,T v) { std::memcpy(static_cast<unsigned char*>(p)+n,&v,sizeof(v)); }
}
int main() {
 try {
    const Region region{true,10.f,20.f};
    check(allowed({false,10,20},5,5,true,5,true),"disabled loop must retain judgments");
    check(allowed(region,10,10,true,10,true) && allowed(region,20,20,true,20,true),"IN and OUT must be inclusive");
    check(!allowed(region,9.99f,10,true,10,true) && !allowed(region,20.01f,20,true,20,true),"buffer song time blocked");
    check(!allowed(region,10.01f,9.99f,true,10.01f,true),"buffer note must not create a late miss inside selection");
    check(!allowed(region,10.01f,10,true,9.99f,true),"early press from buffer must be excluded");
    std::array<Ptr,18> player_secondary{}; player_secondary[17]=reinterpret_cast<Ptr>(&failed);
    std::array<Ptr,1> primary{};
    std::array<Ptr,2> player{reinterpret_cast<Ptr>(primary.data()),reinterpret_cast<Ptr>(player_secondary.data())};
    const Ptr player_ptr=reinterpret_cast<Ptr>(player.data());
    Ptr context=player_ptr;
    std::array<Ptr,3> note_secondary{}; note_secondary[2]=reinterpret_cast<Ptr>(&points);
    std::array<Ptr,2> object{0,reinterpret_cast<Ptr>(note_secondary.data())};
    Ptr note_ptr=reinterpret_cast<Ptr>(object.data());
    std::array<unsigned char,48> point{}; bounds={reinterpret_cast<Ptr>(point.data()),reinterpret_cast<Ptr>(point.data()+point.size())};
    std::array<unsigned char,40> result{};
    put(result.data(),0,3u); put(result.data(),8,.01f); put(result.data(),16,0); put(result.data(),24,static_cast<unsigned char>(1));
    put(result.data(),28,1u); put(result.data(),32,static_cast<unsigned char>(1));
    const auto context_ptr=reinterpret_cast<Ptr>(&context),note=reinterpret_cast<Ptr>(&note_ptr),rec=reinterpret_cast<Ptr>(result.data());
    unsigned char out=9; const auto output=reinterpret_cast<Ptr>(&out); bool suppressed{};
    auto run=[&](float current,float judge) { return dispatch(original,context_ptr,output,note,rec,judge,current,region,player_ptr,
        reinterpret_cast<Ptr>(primary.data()),failed,&suppressed); };
    put(point.data(),0,10.f); all_failed=false;
    check(run(9.f,9.f)==output && suppressed && calls==0 && out==0,"lead-in must not update score or gauge");
    all_failed=true; check(run(21.f,21.f)==output && suppressed && calls==0 && out==1,"tail must preserve native all-failed result");
    all_failed=false; put(point.data(),0,15.f);
    check(run(15.01f,15.01f)==output && !suppressed && calls==1 && out==7,"inside must preserve original callback");
    put(point.data(),0,10.f);
    check(run(10.01f,10.03f)==output && suppressed && calls==1,"press in buffer excluded despite later callback and timing offset");
    put(point.data(),0,9.99f);
    check(run(10.01f,10.01f)==output && suppressed && calls==1,"buffer note cannot create late result inside");
    context=0;
    check(run(9.f,9.f)==output && !suppressed && calls==2 && out==7,"invalid context fails open without altering game behavior");
    std::cout<<"Loop judgment filter scenarios passed\n"; return 0;
 } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
