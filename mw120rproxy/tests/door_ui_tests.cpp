#include "custom_door_ui.h"
#include "replay_bindings.h"
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

std::string hint;
namespace customdoors {
std::string Hint() {
    return hint;
}
}
void Check(bool ok, const char* text) {
    if (!ok) {
        std::printf("FAIL: %s\n", text);
        std::exit(1);
    }
}
int main() {
    auto* image = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 0x10000000, MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Check(image != nullptr, "reserve Replay fixture");
    auto commit = [&](uintptr_t offset) {
        Check(VirtualAlloc(image + (offset & ~uintptr_t(0xFFF)), 0x2000, MEM_COMMIT,
                           PAGE_EXECUTE_READWRITE) != nullptr,
              "commit fixture");
    };
    for (const auto* b :
         {&replay::InteractionPopup, &replay::InteractionString, &replay::InteractionRange}) {
        commit(b->rva);
        memcpy(image + b->rva, b->bytes, b->size);
    }
    commit(0x5A543F8);
    commit(0xF26F940);
    std::array<unsigned char, 0x8000> cg{};
    auto ptr = reinterpret_cast<uintptr_t>(cg.data());
    memcpy(image + 0xF26F940, &ptr, 8);
    memcpy(image + 0xF26F948, &ptr, 8);
    int entity = 2047, range = 7;
    memcpy(cg.data() + 0x2C8, &entity, 4);
    memcpy(cg.data() + 0x2D0, &range, 4);
    // Keep the checked native prologues and range getter. Replace only the
    // complex stock popup/string bodies with bounded, distinguishable results.
    const unsigned char popup[]{0x31, 0xC0, 0x48, 0x81, 0xC4, 0xC8, 0,
                                0,    0,    0x41, 0x5E, 0x5B, 0xC3};
    memcpy(image + replay::InteractionPopup.rva + replay::InteractionPopup.size, popup,
           sizeof(popup));
    const unsigned char string[]{0x41, 0xC6, 0x00, 'S',  0x41, 0xC6, 0x40, 0x01, 0x00,
                                 0xB0, 0x01, 0x48, 0x81, 0xC4, 0x08, 0x01, 0,    0,
                                 0x41, 0x5F, 0x41, 0x5E, 0x5F, 0x5E, 0x5D, 0x5B, 0xC3};
    memcpy(image + replay::InteractionString.rva + replay::InteractionString.size, string,
           sizeof(string));
    const unsigned char rangeTail[]{0x8B, 0x80, 0xD0, 0x02, 0, 0, 0xC3};
    memcpy(image + replay::InteractionRange.rva + replay::InteractionRange.size, rangeTail,
           sizeof(rangeTail));
    Check(customdoorui::Install(reinterpret_cast<uintptr_t>(image)) == hook::Status::Installed,
          "install against exact Replay HUD prologues");
    auto popupCall = reinterpret_cast<bool (*)(int)>(image + replay::InteractionPopup.rva);
    auto stringCall =
        reinterpret_cast<bool (*)(int, size_t, char*)>(image + replay::InteractionString.rva);
    auto rangeCall = reinterpret_cast<unsigned (*)(int)>(image + replay::InteractionRange.rva);
    char text[64]{};
    Check(!popupCall(0) && rangeCall(0) == 7 && stringCall(0, sizeof(text), text) &&
              std::string(text) == "S",
          "stock passthrough with no custom target");
    hint = "Open Door";
    Check(popupCall(0) && rangeCall(0) == 0 && stringCall(0, sizeof(text), text) &&
              std::string(text) == hint,
          "shipped popup receives door text and use range");
    Check(stringCall(0, 1, text) && !text[0], "bounded native output string");
    Check(!stringCall(0, 0, text), "zero capacity rejected");
    hint = "Close Door";
    Check(stringCall(0, sizeof(text), text) && std::string(text) == hint,
          "close label follows motion");
    for (unsigned offset : {0x2C4u, 0x7B83u}) {
        cg[offset] = 1;
        Check(!popupCall(0) && rangeCall(0) == 7 && stringCall(0, sizeof(text), text) &&
                  std::string(text) == "S",
              "native object/mantle prompt takes priority");
        cg[offset] = 0;
    }
    entity = 4;
    memcpy(cg.data() + 0x2C8, &entity, 4);
    Check(!popupCall(0), "existing usable entity keeps its prompt");
    entity = 2047;
    memcpy(cg.data() + 0x2C8, &entity, 4);
    Check(!popupCall(1) && rangeCall(1) == 7, "other local client remains native");
    hint.clear();
    Check(!popupCall(0), "prompt clears on map unload or lost target");
    std::puts(
        "PASS: exact Replay HUD getter ABI, stock popup adapter, key-display passthrough, native prompt priority, bounded text and lifecycle");
}
