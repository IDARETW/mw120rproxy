#pragma once
#include <cstdint>
// Per-thread breadcrumbs read by the exception observer without allocation,
// game callbacks or package/engine locks. Asset-link hooks restore outer scope.
namespace assetcontext {
struct Context {
    bool active = false;
    int type = -1;
    uintptr_t asset = 0, shapeData = 0;
    unsigned shapeBytes = 0;
    char name[160]{};
    const char* phase = "none";
};
inline thread_local Context current{};
}
