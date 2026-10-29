#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace training::post_effect {
using Ptr=std::uintptr_t;
using EventHandler=Ptr(__fastcall*)(Ptr,Ptr);
using ShakeUpdate=bool(__fastcall*)(Ptr,const float*);

// FumenEvent is the guarded native 272-byte variant; tag 40 carries a
// post-effect command. Forward all other event batches without copying variants.
inline Ptr dispatch(EventHandler original,Ptr self,Ptr vector,bool seeking) {
    if (!seeking || !vector) return original(self,vector);
    const auto* bounds=reinterpret_cast<const Ptr*>(vector);
    const Ptr begin=bounds[0],end=bounds[1];
    if (end<begin || (end-begin)%272 || (end-begin)/272>100000) return original(self,vector);
    Ptr result=0,segment=begin;
    for(Ptr item=begin;item<end;item+=272) {
        unsigned type{}; std::memcpy(&type,reinterpret_cast<const void*>(item),sizeof(type));
        if(type!=40) continue;
        if(segment!=item) { Ptr range[]{segment,item,item}; result=original(self,reinterpret_cast<Ptr>(range)); }
        segment=item+272;
    }
    if(segment!=end) { Ptr range[]{segment,end,end}; result=original(self,reinterpret_cast<Ptr>(range)); }
    return result;
}
// The native RandomShake callback applies random displacement whenever time is
// before its end, even when it is earlier than its start. Asking it to evaluate
// at its own end resets the layer to (0,0) and lets native code dispose it.
inline bool update_shake(ShakeUpdate original,Ptr self,const float* time,bool suppress) {
    float finish{}; bool terminate=false;
    __try {
        const Ptr curve=*reinterpret_cast<const Ptr*>(self+24);
        if(curve && time && std::isfinite(*time)) {
            const Ptr table=*reinterpret_cast<const Ptr*>(curve);
            using Endpoint=void*(__fastcall*)(Ptr,void*);
            const auto start_fn=reinterpret_cast<Endpoint>(*reinterpret_cast<const Ptr*>(table+16));
            const auto end_fn=reinterpret_cast<Endpoint>(*reinterpret_cast<const Ptr*>(table+24));
            std::uint64_t first{},last{};
            if(start_fn && end_fn && start_fn(curve,&first) && end_fn(curve,&last)) {
                float start{}; std::memcpy(&start,&first,sizeof(start)); std::memcpy(&finish,&last,sizeof(finish));
                terminate=std::isfinite(start) && std::isfinite(finish) && finish>=start && (suppress || *time<start);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { terminate=false; }
    return original(self,terminate?&finish:time);
}
}
