#include "custom_door_ui.h"
#include "custom_doors.h"
#include "replay_bindings.h"
#include "safemem.h"
#include <atomic>
#include <cstring>

namespace {
uintptr_t base = 0;
using Popup = bool (*)(int);
using String = bool (*)(int, size_t, char*);
using Range = unsigned (*)(int);
std::atomic<Popup> originalPopup{nullptr};
std::atomic<String> originalString{nullptr};
std::atomic<Range> originalRange{nullptr};

std::string Hint(int client) {
    if (client != 0)
        return {};
    auto hint = customdoors::Hint();
    if (hint.empty())
        return {};
    uintptr_t cg = 0;
    unsigned char type = 255, mantle = 1;
    int entity = 0;
    // Exact Replay HUD getters read the predicted cursor hint from cg_t.
    // Existing weapon, objective, mantle and native door prompts take priority.
    if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0xF26F940), &cg, 8) || !cg ||
        !safemem::ReadBytes(reinterpret_cast<void*>(cg + 0x2C4), &type, 1) || type ||
        !safemem::ReadBytes(reinterpret_cast<void*>(cg + 0x2C8), &entity, 4) || entity != 2047 ||
        !safemem::ReadBytes(reinterpret_cast<void*>(cg + 0x7B83), &mantle, 1) || mantle)
        return {};
    return hint;
}
bool HasPopup(int client) {
    return !Hint(client).empty() || originalPopup.load()(client);
}
bool Text(int client, size_t capacity, char* buffer) {
    const auto hint = Hint(client);
    if (hint.empty())
        return originalString.load()(client, capacity, buffer);
    if (!buffer || !capacity)
        return false;
    const size_t count = (std::min)(capacity - 1, hint.size());
    memcpy(buffer, hint.data(), count);
    buffer[count] = 0;
    return true;
}
unsigned UseRange(int client) {
    return Hint(client).empty() ? originalRange.load()(client) : 0;
}
}
namespace customdoorui {
hook::Status Install(uintptr_t address) {
    base = address;
    // Feed the existing interaction widget through its native getters. The
    // shipped key binding, controller glyph, typography and layout stay native.
    auto status =
        hook::Install(reinterpret_cast<void*>(base + replay::InteractionPopup.rva), &HasPopup,
                      replay::InteractionPopup.bytes, replay::InteractionPopup.size, originalPopup);
    if (status == hook::Status::Installed)
        status = hook::Install(reinterpret_cast<void*>(base + replay::InteractionString.rva), &Text,
                               replay::InteractionString.bytes, replay::InteractionString.size,
                               originalString);
    if (status == hook::Status::Installed)
        status = hook::Install(reinterpret_cast<void*>(base + replay::InteractionRange.rva),
                               &UseRange, replay::InteractionRange.bytes,
                               replay::InteractionRange.size, originalRange);
    return status;
}
}
