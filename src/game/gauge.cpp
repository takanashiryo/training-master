#include "game/gauge.hpp"
#include <windows.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
namespace training::gauge {
namespace {
template<class T> T& at(Ptr p,std::size_t n) { return *reinterpret_cast<T*>(p+n); }
struct Deque { Ptr proxy{},map{}; std::size_t size{},head{},count{}; };
struct Vector { Ptr begin{},end{},capacity{}; };
struct Record { float time{},hp{}; unsigned char clear{},failed{},padding[2]{}; };
static_assert(sizeof(Deque)==40 && sizeof(Record)==12);
bool valid(const Deque& d,Ptr address) {
    return d.proxy && at<Ptr>(d.proxy,0)==address && !at<Ptr>(d.proxy,8) &&
        d.map && d.size>=8 && d.size<=1024 && !(d.size&(d.size-1)) && d.count && d.count<=8;
}
Ptr model(const Deque& d,std::size_t i) { return at<Ptr>(at<Ptr>(d.map,8*((d.head+i)&(d.size-1))),0); }
bool history(Ptr p) {
    const auto v=at<Vector>(p,0);
    return v.begin && v.end>=v.begin+12 && v.capacity>=v.end && (v.end-v.begin)%12==0 &&
        (v.capacity-v.begin)/12<=10000000 && std::isfinite(at<float>(v.end-12,4)) &&
        at<float>(v.end-12,4)>=0 && at<float>(v.end-12,4)<=100;
}
void swap_queue(Ptr a,Ptr b) {
    const auto temp=at<Deque>(a,0); at<Deque>(a,0)=at<Deque>(b,0); at<Deque>(b,0)=temp;
    if(at<Deque>(a,0).proxy) at<Ptr>(at<Deque>(a,0).proxy,0)=a;
    if(at<Deque>(b,0).proxy) at<Ptr>(at<Deque>(b,0).proxy,0)=b;
}
bool initialize(Layout l,Ptr address) {
    auto& d=at<Deque>(address,0); d={}; d.proxy=l.allocate(16);
    if(!d.proxy) return false;
    at<Ptr>(d.proxy,0)=address; at<Ptr>(d.proxy,8)=0; return true;
}
std::size_t types(GaugeType t,std::uint64_t* out) {
    switch(t) {
    case GaugeType::Effective: out[0]=0; return 1;
    case GaugeType::Excessive: out[0]=2; return 1;
    case GaugeType::Maxxive: out[0]=6; return 1;
    case GaugeType::Permissive: out[0]=1; return 1;
    case GaugeType::ExcessiveArs: out[0]=0; out[1]=2; return 2;
    case GaugeType::MaxxiveArs: out[0]=0; out[1]=2; out[2]=6; return 3;
    default: return 0;
    }
}
}
bool Controller::bind(Ptr world,Binding& b) const {
    if(!world || !layout_.allocate || !layout_.push || !layout_.build || !layout_.destroy_player ||
       !layout_.destroy_types || (layout_.chart_gauge_gain && !layout_.options)) return false;
    __try {
        b={world,at<Ptr>(world,200),0,at<Ptr>(world,152)};
        if(at<Ptr>(world,0)!=layout_.world_vtable || !b.player || !b.chart ||
           at<Ptr>(b.player,0)!=layout_.player_vtable || at<Ptr>(b.chart,0)!=layout_.chart_vtable) return false;
        b.objects=at<Ptr>(b.player,24);
        if(!b.objects) return false;
        const auto notes=at<Vector>(b.objects,80);
        if(notes.end<notes.begin || notes.capacity<notes.end || (notes.end-notes.begin)%16 ||
           (notes.end-notes.begin)/16>1000000) return false;
        const auto d=at<Deque>(b.player,88),t=at<Deque>(world,layout_.world.gauge_types);
        if(!valid(d,b.player+88) || !valid(t,world+layout_.world.gauge_types)) return false;
        for(std::size_t i=0;i<d.count;++i) {
            const Ptr block=at<Ptr>(d.map,8*((d.head+i)&(d.size-1)));
            const Ptr p=at<Ptr>(block,0),ref=at<Ptr>(block,8);
            if(!ref || p!=ref+16 || at<Ptr>(ref,0)!=layout_.ref_vtable || at<unsigned>(p,24)>7 || !history(p)) return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Controller::available(Ptr world) const { Binding b; return bind(world,b); }
bool Controller::replace(const Binding& b,GaugeConfig config) {
    alignas(8) unsigned char player[208]{},type_queue[40]{};
    const Ptr p=reinterpret_cast<Ptr>(player),t=reinterpret_cast<Ptr>(type_queue);
    bool success=false;
    __try {
        __try {
            std::uint64_t values[3]{},gain{},initial=std::bit_cast<std::uint32_t>(config.initial)|(std::uint64_t{1}<<32);
            const auto count=types(config.type,values);
            if(count && initialize(layout_,t) && initialize(layout_,p+88)) {
                for(std::size_t i=0;i<count;++i) layout_.push(t,&values[i]);
                at<Ptr>(p,24)=b.objects; // Borrow notes, never their shared ownership.
                if(layout_.chart_gauge_gain) {
                    layout_.options(b.chart,reinterpret_cast<Ptr>(&gain));
                    layout_.build(p,t,&initial,&gain);
                } else {
                    using LegacyBuild=Ptr(__fastcall*)(Ptr,Ptr,const std::uint64_t*);
                    reinterpret_cast<LegacyBuild>(layout_.build)(p,t,&initial);
                }
                auto& built=at<Deque>(p,88);
                bool complete=valid(built,p+88) && built.count==count;
                for(std::size_t i=0;complete && i<count;++i)
                    complete=at<std::uint64_t>(model(built,i),24)==values[i] && history(model(built,i));
                if(complete) {
                    swap_queue(b.player+88,p+88); swap_queue(b.world+layout_.world.gauge_types,t);
                    if(!active_) {
                        swap_queue(p+88,reinterpret_cast<Ptr>(saved_player_)+88);
                        swap_queue(t,reinterpret_cast<Ptr>(saved_types_));
                    }
                    active_=true; binding_=b; config_=config; refresh_world_=b.world; success=true;
                }
            }
        } __finally {
            // The non-deleting native destructor releases only owned members.
            layout_.destroy_player(p); layout_.destroy_types(t);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { success=false; }
    return success;
}
bool Controller::reset_models(const Binding& b) {
    __try {
        const auto d=at<Deque>(b.player,88);
        for(std::size_t i=0;i<d.count;++i) {
            const Ptr p=model(d,i); const bool primary=i+1==d.count;
            const float hp=active_?(primary?config_.initial:at<float>(p,56)):
                (at<unsigned char>(p,132)?at<float>(p,128):at<float>(p,56));
            if(!std::isfinite(hp) || hp<0 || hp>100 || !history(p)) return false;
        }
        for(std::size_t i=0;i<d.count;++i) {
            const Ptr p=model(d,i); const bool primary=i+1==d.count;
            const float hp=active_?(primary?config_.initial:at<float>(p,56)):
                (at<unsigned char>(p,132)?at<float>(p,128):at<float>(p,56));
            if(active_) at<std::uint64_t>(p,128)=primary?(std::bit_cast<std::uint32_t>(hp)|(std::uint64_t{1}<<32)):0;
            auto& v=at<Vector>(p,0);
            at<Record>(v.begin,0)={0.f,hp,static_cast<unsigned char>(hp>=at<float>(p,60)),0,{}};
            v.end=v.begin+12;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Controller::set(Ptr world,GaugeConfig config) {
    if(!config.enabled) return release(world);
    if(config.type< GaugeType::Effective || config.type>=GaugeType::Count ||
       !std::isfinite(config.initial) || config.initial<0 || config.initial>100) return false;
    Binding b;
    if(!bind(world,b) || (active_ && (b.world!=binding_.world || b.player!=binding_.player))) return false;
    if(!active_ || config.type!=config_.type) return replace(b,config);
    config_=config; return reset_models(b);
}
bool Controller::reset(Ptr world) {
    Binding b;
    return bind(world,b) && (!active_ || (b.world==binding_.world && b.player==binding_.player)) && reset_models(b);
}
bool Controller::failed(Ptr world,bool& value) const {
    value=false; Binding b;
    if(!layout_.failed || !bind(world,b)) return false;
    __try { value=layout_.failed(b.player+8); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Controller::after_events(Ptr hud_interface,Ptr events) {
    if(!refresh_world_ || !layout_.refresh) return true;
    __try {
        const Ptr hud=hud_interface-8;
        if(at<Ptr>(hud,16)!=refresh_world_+layout_.world.music) return true;
        const Ptr begin=at<Ptr>(events,0),end=at<Ptr>(events,8);
        if(end<begin || (end-begin)%272 || (end-begin)/272>10000) return false;
        for(Ptr p=begin;p<end;p+=272) if(at<unsigned>(p,0)==12 && at<unsigned char>(p,264)==11) {
            // Native handler rebuilds on normalized type changes only. The ARS
            // variant can change with the same type, so refresh its source art.
            layout_.refresh(hud); refresh_world_=0; break;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Controller::release(Ptr world) {
    if(!active_) return true;
    Binding b;
    if(!bind(world,b) || b.world!=binding_.world || b.player!=binding_.player) return false;
    __try {
        swap_queue(b.player+88,reinterpret_cast<Ptr>(saved_player_)+88);
        swap_queue(world+layout_.world.gauge_types,reinterpret_cast<Ptr>(saved_types_));
        active_=false; refresh_world_=world;
        layout_.destroy_player(reinterpret_cast<Ptr>(saved_player_));
        layout_.destroy_types(reinterpret_cast<Ptr>(saved_types_));
        std::memset(saved_player_,0,sizeof(saved_player_)); std::memset(saved_types_,0,sizeof(saved_types_));
        binding_={}; config_={}; return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
