#include "noclip.h"
#include "replay_bindings.h"
#include "safemem.h"
#include "logger.h"
#include <cstring>

namespace {
uintptr_t base = 0;
using ClientCommand = void (*)(int);
std::atomic<ClientCommand> original{nullptr};
template <class Fn> Fn Function(const replay::Binding& b) {
    return reinterpret_cast<Fn>(base + b.rva);
}
void Command(int clientNum) {
    char name[32]{}, value[32]{}, extra[2]{};
    auto argv = Function<void (*)(int, char*, size_t)>(replay::ServerArgv);
    argv(0, name, sizeof(name));
    if (_stricmp(name, "noclip") && _stricmp(name, "mw_noclip")) {
        original.load()(clientNum);
        return;
    }
    // Execute on the native server command path, for the local host only.
    // No frame polling or cached player pointer survives a map/respawn change.
    if (clientNum != 0 || Function<bool (*)()>(replay::IsFrontEnd)() ||
        !Function<bool (*)(const char*)>(replay::GetBool)("LPSPMQSNPQ")) {
        LOG_WARN("Noclip", "available to player zero in an active Local Play match");
        return;
    }
    argv(1, value, sizeof(value));
    argv(2, extra, sizeof(extra));
    const bool on = !_stricmp(value, "on") || !strcmp(value, "1");
    const bool off = !_stricmp(value, "off") || !strcmp(value, "0");
    if (extra[0] || (value[0] && !on && !off)) {
        LOG_WARN("Noclip", "usage: noclip [on|off|1|0]");
        return;
    }
    uintptr_t entities = 0, client = 0;
    unsigned flags = 0;
    int pmType = -1;
    if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0xBC20F00), &entities, 8) || !entities ||
        !safemem::ReadBytes(reinterpret_cast<void*>(entities + 0x150), &client, 8) || !client ||
        !safemem::ReadBytes(reinterpret_cast<void*>(client + 0xC), &pmType, 4) ||
        (pmType != 0 && pmType != 2) ||
        !safemem::ReadBytes(reinterpret_cast<void*>(client + 0x5DD0), &flags, 4)) {
        LOG_WARN("Noclip", "spawn as a player before changing noclip");
        return;
    }
    // Replay 11E801B tests flags bit 0, then sets ps.pm_type (+C) to 2.
    // Change only that server flag; stock movement and snapshots do the rest.
    const bool enable = value[0] ? on : !(flags & 1);
    const unsigned next = enable ? (flags | 1) : (flags & ~1u);
    if (safemem::WriteBytes(reinterpret_cast<void*>(client + 0x5DD0), &next, 4))
        LOG_INFO("Noclip", "%s for local player; native movement flag %08X -> %08X",
                 enable ? "ON" : "OFF", flags, next);
    else
        LOG_WARN("Noclip", "player flag was not writable; state unchanged");
}
}
namespace noclip {
hook::Status Install(uintptr_t address) {
    if (original.load())
        return hook::Status::Installed;
    base = address;
    for (const auto* b : {&replay::ServerArgv, &replay::NoclipMovementFlags, &replay::IsFrontEnd,
                          &replay::GetBool}) {
        unsigned char data[64]{};
        if (!safemem::ReadBytes(reinterpret_cast<void*>(base + b->rva), data, b->size) ||
            memcmp(data, b->bytes, b->size))
            return hook::Status::NotReady;
    }
    const auto& b = replay::ClientCommand;
    auto result =
        hook::Install(reinterpret_cast<void*>(base + b.rva), &Command, b.bytes, b.size, original);
    if (result == hook::Status::Installed)
        LOG_INFO("Noclip", "local server command installed: noclip [on|off|1|0]");
    return result;
}
}
