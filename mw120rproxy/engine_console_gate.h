#pragma once
#include <cstdint>
namespace engineconsole {
// Bounded console-only filtering. The engine file and native console retain the
// original messages; this gate only protects the external development window.
struct Gate {
    struct Recent {uint64_t hash=0,time=0;bool used=false;} recent[128]{};
    uint64_t window=0;unsigned normal=0,errors=0;
    bool Accept(uint64_t hash,bool error,uint64_t now) {
        if(now/1000!=window){window=now/1000;normal=errors=0;}
        auto& r=recent[hash%128];
        if(r.used && r.hash==hash && now-r.time<1000)return false;
        auto& count=error?errors:normal;
        if(count>=(error?32u:128u))return false;
        ++count;r={hash,now,true};return true;
    }
};
inline uint64_t Hash(unsigned channel,int flags,const char* message) {
    uint64_t value=14695981039346656037ull^channel^(uint64_t(unsigned(flags))<<32);
    for(const auto* p=message;*p;++p){value^=static_cast<unsigned char>(*p);value*=1099511628211ull;}
    return value;
}
}
