#include "custom_doors.h"
#include "developer_ui.h"
#include "custom_map_ui.h"
#include "custom_glass.h"
#include "command_text.h"
#include "custom_maps.h"
#include "custom_map_loader.h"
#include "replay_bindings.h"
#include "logger.h"
#include "safemem.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace {
using Overlay = uintptr_t(__fastcall*)(uintptr_t, int);
using KeyEvent = void(__fastcall*)(int, int, bool, unsigned, int, int);
using AddText = void(__fastcall*)(int, const char*);
using DrawText = void(__fastcall*)(const void*,
                                   const char*,
                                   int,
                                   const void*,
                                   float,
                                   float,
                                   int,
                                   int,
                                   float,
                                   float,
                                   const float*,
                                   int);
using DrawPic = void(__fastcall*)(
    float, float, float, float, float, float, float, float, const float*, const void*);
std::atomic<Overlay> g_overlay{nullptr};
std::atomic<KeyEvent> g_keyEvent{nullptr};
std::atomic<AddText> g_addText{nullptr};
std::atomic<bool> g_ready{false};
std::atomic<bool> g_openRequested{false};
uintptr_t g_base = 0;
// These fields are owned by the game's main thread (checked at both entry points).
bool g_browser = false;
bool g_consumed[256]{};
bool g_shift[2]{};
bool g_receivedOverlay = false;
std::vector<custommaps::Package> g_packages;
size_t g_selection = 0;
std::string g_status = "Select a package, then use Start Match in Local Play.";
std::string g_previousMap;
std::string g_previousMode;
std::string g_pendingMap;
unsigned g_pendingFrames = 0;

template <class Fn> Fn Function(const replay::Binding& binding) {
    return reinterpret_cast<Fn>(g_base + binding.rva);
}
bool MainThread() {
    return Function<bool (*)()>(replay::IsMainThread)();
}
bool ConsoleActive() {
    return Function<bool (*)(int)>(replay::ConsoleActive)(0);
}
void ToggleConsole() {
    Function<void (*)()>(replay::ToggleConsole)();
}
bool CanSelect() {
    return Function<bool (*)()>(replay::IsFrontEnd)() &&
           Function<bool (*)(const char*)>(replay::GetBool)("LPSPMQSNPQ"); // systemlink
}
std::string DvarString(const char* token) {
    char value[128]{};
    safemem::ReadString(Function<const char* (*)(const char*)>(replay::GetString)(token), value,
                        sizeof(value));
    return value;
}
bool SafeName(const std::string& value) {
    return !value.empty() && value.size() <= 63 &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
           });
}
bool Submit(const std::string& text) {
    if (!MainThread() || !g_ready.load(std::memory_order_acquire))
        return false;
    // Append to local client zero exactly like the native inlined Cbuf_AddText
    // path, under CRITSECT_CBUF. Replay's standalone debug append routine
    // starts at the next buffer slot and drops commands during local matches.
    struct Buffer {
        char* data;
        int capacity;
        int size;
    } buffer{};
    auto enter = Function<void (*)(int)>(replay::EnterCritical);
    auto leave = Function<void (*)(int)>(replay::LeaveCritical);
    enter(35);
    const bool readable = safemem::ReadBytes(
        reinterpret_cast<void*>(g_base + replay::ConsoleBuffer), &buffer, sizeof(buffer));
    const bool room = readable && buffer.data && buffer.capacity == 0x10000 && buffer.size >= 0 &&
                      buffer.size < buffer.capacity &&
                      text.size() < static_cast<size_t>(buffer.capacity - buffer.size);
    bool queued = false;
    if (room && safemem::WriteBytes(buffer.data + buffer.size, text.c_str(), text.size() + 1)) {
        const int next = buffer.size + static_cast<int>(text.size());
        queued = safemem::WriteBytes(reinterpret_cast<void*>(g_base + replay::ConsoleBuffer + 12),
                                     &next, sizeof(next));
    }
    leave(35);
    if (queued)
        LOG_INFO("Cbuf", "queued for local client zero on the game main thread: %s", text.c_str());
    else
        LOG_WARN("Cbuf", "command not queued: native buffer is unavailable or full");
    return queued;
}
void Refresh() {
    if (!CanSelect()) {
        g_status = "Return to the Local Play lobby before refreshing packages.";
        return;
    }
    custommaps::Refresh();
    g_packages = custommaps::List();
    if (g_selection >= g_packages.size())
        g_selection = 0;
    g_status = g_packages.empty()
                   ? "No packages found in mods/mw120r/maps."
                   : "Choose a map, press Enter, then close this browser and start the match.";
}
void SetBrowser(bool open) {
    if (open == g_browser)
        return;
    g_browser = open;
    // Borrow the stock console catcher so mouse/character input cannot reach the
    // lobby or gameplay while the browser is open. Toggle clears the native field.
    if (open) {
        if (!ConsoleActive())
            ToggleConsole();
        Refresh();
    } else if (ConsoleActive())
        ToggleConsole();
}
void SelectPackage() {
    if (!CanSelect()) {
        g_status = "Open Multiplayer > Local Play before selecting a custom map.";
        return;
    }
    if (!customloader::Ready()) {
        g_status = "Custom package disk routing is unavailable. Check the log.";
        return;
    }
    if (g_packages.empty()) {
        g_status = "No map package is selected.";
        return;
    }
    const auto& package = g_packages[g_selection];
    if (!package.valid) {
        g_status = package.error;
        return;
    }
    if (g_pendingFrames) {
        g_status = "Waiting for the current map selection to apply.";
        return;
    }
    const auto oldSelection = custommaps::Active();
    const auto oldMap = DvarString("NSQLTTMRMP");
    const auto oldMode = DvarString("MOLPOSLOMO");
    if (oldSelection.empty() && SafeName(oldMap) && SafeName(oldMode)) {
        g_previousMap = oldMap;
        g_previousMode = oldMode;
    }
    if (!custommaps::Select(package.id.c_str())) {
        g_status = "Package changed. Press R to refresh.";
        return;
    }
    // Package manifests call this mode "tdm"; the game names it "war".
    if (!Submit("set NSQLTTMRMP " + package.id + "\nset MOLPOSLOMO war\n")) {
        if (oldSelection.empty())
            custommaps::ClearSelection();
        else
            custommaps::Select(oldSelection.c_str());
        g_status = "Command buffer unavailable; selection was not applied.";
        return;
    }
    g_pendingMap = package.id;
    g_pendingFrames = 300;
    g_status = "Applying map selection through the game command buffer...";
}
void RestoreStock() {
    if (!CanSelect()) {
        g_status = "Return to the Local Play lobby before switching maps.";
        return;
    }
    if (!SafeName(g_previousMap) || !SafeName(g_previousMode)) {
        g_status = "Choose a stock map in the normal Local Play map menu.";
        return;
    }
    if (!Submit("set NSQLTTMRMP " + g_previousMap + "\nset MOLPOSLOMO " + g_previousMode + "\n")) {
        g_status = "Command buffer unavailable; retry after the lobby loads.";
        return;
    }
    custommaps::ClearSelection();
    g_pendingMap.clear();
    g_pendingFrames = 0;
    g_status = "Restored the previous stock map and game type.";
}
// Local file endpoint for repeatable offline testing. Requests are explicitly
// addressed to this PID; all engine work runs here on the game main thread.
// No socket, arbitrary native calls, or writes to game memory are exposed.
void PollControl() {
    static ULONGLONG lastPoll = 0;
    static unsigned lastId = 0;
    static int heldKey = 0, heldVirtual = 0;
    static ULONGLONG releaseAt = 0;
    const auto now = GetTickCount64();
    if (heldKey && now >= releaseAt) {
        g_keyEvent.load()(0, heldKey, false, GetTickCount(), heldVirtual, 0);
        heldKey = heldVirtual = 0;
    }
    if (now - lastPoll < 250)
        return;
    lastPoll = now;
    wchar_t root[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, root, MAX_PATH))
        return;
    auto* tail = wcsrchr(root, L'\\');
    if (!tail)
        return;
    tail[1] = 0;
    const std::wstring request = std::wstring(root) + L".proxy\\control.ini";
    if (GetPrivateProfileIntW(L"request", L"pid", 0, request.c_str()) != GetCurrentProcessId())
        return;
    const unsigned id = GetPrivateProfileIntW(L"request", L"id", 0, request.c_str());
    if (!id || id == lastId)
        return;
    lastId = id;
    wchar_t action[32]{}, value[4096]{};
    GetPrivateProfileStringW(L"request", L"action", L"status", action, 32, request.c_str());
    GetPrivateProfileStringW(L"request", L"value", L"", value, 4096, request.c_str());
    std::string text;
    for (const auto ch : std::wstring(value)) {
        if (ch < 32 || ch > 126) {
            text.clear();
            break;
        }
        text += static_cast<char>(ch);
    }
    bool ok = false;
    std::string payload;
    if (wcscmp(action, L"status") == 0)
        ok = true;
    else if (wcscmp(action, L"hotkey") == 0 &&
             Function<bool (*)(const char*)>(replay::GetBool)("LPSPMQSNPQ")) {
        // Exercise the same detoured handler as physical keys. This is a
        // fixed UI-key allowlist, independent of held gameplay key requests.
        int key = 0, vk = 0;
        if (text == "f6") {
            key = 0x9F;
            vk = VK_F6;
        } else if (text == "f7") {
            key = 0xA0;
            vk = VK_F7;
        } else if (text == "grave") {
            key = 96;
            vk = VK_OEM_3;
        } else if (text == "enter") {
            key = 13;
            vk = VK_RETURN;
        } else if (text == "escape") {
            key = 27;
            vk = VK_ESCAPE;
        } else if (text == "up") {
            key = 132;
            vk = VK_UP;
        } else if (text == "down") {
            key = 133;
            vk = VK_DOWN;
        } else if (text == "backspace") {
            key = 8;
            vk = VK_BACK;
        }
        if (key) {
            auto dispatch =
                Function<void (*)(int, int, bool, unsigned, int, int)>(replay::KeyEvent);
            dispatch(0, key, true, GetTickCount(), vk, 0);
            dispatch(0, key, false, GetTickCount(), vk, 0);
            ok = true;
        }
    } else if (wcscmp(action, L"key") == 0 && !heldKey && !CanSelect() && !ConsoleActive() &&
               Function<bool (*)(const char*)>(replay::GetBool)("LPSPMQSNPQ")) {
        // A bounded press through the existing native key handler. Always
        // release from the frame callback, independently of later requests.
        char name[16]{};
        unsigned duration = 0;
        if (sscanf_s(text.c_str(), "%15s %u", name, unsigned(sizeof(name)), &duration) == 2 &&
            duration >= 50 && duration <= 5000) {
            int key = 0, vk = 0;
            if (strlen(name) == 1 && strchr("wasd", name[0])) {
                key = name[0];
                vk = key - 'a' + 'A';
            } else if (strcmp(name, "space") == 0) {
                key = 32;
                vk = VK_SPACE;
            } else if (strcmp(name, "escape") == 0) {
                key = 27;
                vk = VK_ESCAPE;
            }
            if (key) {
                heldKey = key;
                heldVirtual = vk;
                releaseAt = now + duration;
                g_keyEvent.load()(0, key, true, GetTickCount(), vk, 0);
                ok = true;
            }
        }
    } else if (wcscmp(action, L"player") == 0 && !Function<bool (*)()>(replay::IsFrontEnd)()) {
        uintptr_t entities = 0, client = 0;
        float origin[3]{}, entityOrigin[3]{};
        if (safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0xBC20F00), &entities, 8) &&
            entities && safemem::ReadBytes(reinterpret_cast<void*>(entities + 0x150), &client, 8) &&
            client &&
            safemem::ReadBytes(reinterpret_cast<void*>(client + 0x30), origin, sizeof(origin)) &&
            safemem::ReadBytes(reinterpret_cast<void*>(entities + 0x130), entityOrigin,
                               sizeof(entityOrigin))) {
            std::ostringstream out;
            out << "player_origin=" << origin[0] << "," << origin[1] << "," << origin[2]
                << " entity_origin=" << entityOrigin[0] << "," << entityOrigin[1] << ","
                << entityOrigin[2];
            payload = out.str();
            ok = true;
        }
    } else if (wcscmp(action, L"console") == 0 &&
               (text == "open" || text == "close" || text == "toggle-output")) {
        const bool desired = text != "close";
        if (desired && g_browser)
            SetBrowser(false);
        if (ConsoleActive() != desired)
            ToggleConsole();
        if (text == "toggle-output")
            Function<void (*)()>(replay::ToggleOutput)();
        ok = ConsoleActive() == desired;
    } else if (wcscmp(action, L"browser") == 0 && (text == "open" || text == "close")) {
        SetBrowser(text == "open");
        ok = g_browser == (text == "open");
    } else if (wcscmp(action, L"command") == 0 && !text.empty() &&
               Function<bool (*)(const char*)>(replay::GetBool)("LPSPMQSNPQ"))
        ok = Submit(commandtext::Translate(text) + "\n");
    else if (wcscmp(action, L"select") == 0 && CanSelect() && SafeName(text)) {
        Refresh();
        for (size_t i = 0; i < g_packages.size(); ++i)
            if (g_packages[i].id == text) {
                g_selection = i;
                SelectPackage();
                ok = g_pendingFrames != 0;
                break;
            }
    } else if (wcscmp(action, L"read") == 0) {
        // Image-relative, bounded read for inspecting this supported debug EXE.
        unsigned long long rva = 0;
        unsigned length = 0;
        if (sscanf_s(text.c_str(), "%llx %u", &rva, &length) == 2 && length && length <= 512 &&
            rva < 0x1324B000 && length <= 0x1324B000 - rva) {
            unsigned char bytes[512]{};
            ok = safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), bytes, length);
            if (ok) {
                const char* digits = "0123456789abcdef";
                for (unsigned i = 0; i < length; ++i) {
                    payload += digits[bytes[i] >> 4];
                    payload += digits[bytes[i] & 15];
                }
            }
        }
    }
    const auto map = DvarString("NSQLTTMRMP");
    std::ostringstream response;
    response << "{\"pid\":" << GetCurrentProcessId() << ",\"id\":" << id
             << ",\"ok\":" << (ok ? "true" : "false")
             << ",\"frontend\":" << (Function<bool (*)()>(replay::IsFrontEnd)() ? "true" : "false")
             << ",\"map\":\"" << (SafeName(map) ? map : "") << "\",\"selected\":\""
             << custommaps::Active() << "\",\"console\":" << (ConsoleActive() ? "true" : "false")
             << ",\"browser\":" << (g_browser ? "true" : "false") << ",\"data\":\"" << payload
             << "\"}\n";
    const std::wstring result = std::wstring(root) + L".proxy\\control-result.json";
    const std::wstring staged = result + L".tmp";
    {
        std::ofstream output(staged, std::ios::binary);
        output << response.str();
    }
    MoveFileExW(staged.c_str(), result.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    LOG_INFO("Control", "request=%u action=%ls accepted=%d", id, action, int(ok));
}
std::string DisplayText(const std::string& text, size_t maximum = 94) {
    std::string result;
    for (unsigned char c : text) {
        if (result.size() >= maximum)
            break;
        result += c >= 32 && c < 127 && c != '^' ? static_cast<char>(c) : ' ';
    }
    return result;
}
void DrawBrowser() {
    uintptr_t font = 0;
    if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + replay::UIFont), &font,
                            sizeof(font)) ||
        !font)
        return;
    const float white[] = {0.9f, 0.94f, 0.97f, 1};
    const float cyan[] = {0.35f, 0.84f, 1, 1};
    const float muted[] = {0.62f, 0.7f, 0.76f, 1};
    const float dark[] = {0.025f, 0.04f, 0.055f, 0.98f};
    // Lookup once per browser frame rather than retaining an asset pointer across
    // frontend zone unloads. This is a native DB query on the main thread.
    auto* material = Function<void* (*)(int, const char*, int)>(replay::FindAsset)(11, "white", 0);
    if (material)
        Function<DrawPic>(replay::DrawPic)(0, 0, 8192, 8192, 0, 0, 0, 0, dark, material);
    auto line = [&](const std::string& text, float y, const float* color, float scale = 0.5f) {
        const auto visible = DisplayText(text);
        Function<DrawText>(replay::DrawText)(
            reinterpret_cast<void*>(g_base + replay::ScreenPlacement), visible.c_str(), 0x7FFFFFFF,
            reinterpret_cast<void*>(font), 24, y, 1, 1, scale, scale, color, 7);
    };
    line("CUSTOM MAPS", 42, cyan, 0.9f);
    line("Up / Down: browse   Enter: select   R: refresh   Backspace: stock   F6 / Esc: close", 66,
         muted, 0.45f);
    line("Package folder: mods/mw120r/maps/<map-id>", 87, muted, 0.45f);
    const size_t first = g_selection >= 8 ? g_selection - 7 : 0;
    for (size_t i = first; i < g_packages.size() && i < first + 8; ++i) {
        const auto& package = g_packages[i];
        line(std::string(i == g_selection ? "> " : "  ") + package.title + "  [" + package.id +
                 "]" + (package.valid ? "" : "  unavailable"),
             122 + 25 * float(i - first), i == g_selection ? cyan : white);
    }
    if (!g_packages.empty()) {
        const auto& package = g_packages[g_selection];
        line(package.valid ? package.description : package.error, 340, muted, 0.45f);
    }
    line(g_status, 379, white, 0.48f);
    line("Local Play map: " + DvarString("NSQLTTMRMP"), 407, cyan, 0.5f);
    line("After selecting: close this browser and choose Start Match in the lobby.", 435, muted,
         0.45f);
    line("~ or F7 opens the game console. Friendly dvar names are translated for Replay.", 456,
         muted, 0.45f);
}
uintptr_t __fastcall DrawOverlay(uintptr_t self, int localClient) {
    const auto result = g_overlay.load(std::memory_order_acquire)(self, localClient);
    if (!g_ready.load(std::memory_order_acquire) || localClient != 0 || !MainThread())
        return result;
    try {
        PollControl();
        customglass::PumpEffects();
        customdoors::PumpSounds();
        if (!g_pendingFrames && CanSelect())
            custommapui::SyncSelection(DvarString("NSQLTTMRMP").c_str());
        if (!g_receivedOverlay) {
            g_receivedOverlay = true;
            LOG_INFO(
                "UI",
                "native MP overlay reached; tilde/F7 console and F6 custom-map browser available");
        }
        if (g_openRequested.exchange(false))
            SetBrowser(true);
        if (g_pendingFrames) {
            if (DvarString("NSQLTTMRMP") == g_pendingMap) {
                g_status = "Selected " + g_pendingMap + ". Close the browser, then Start Match.";
                LOG_INFO("Maps", "native ui_mapname readback confirmed '%s'", g_pendingMap.c_str());
                g_pendingFrames = 0;
            } else if (--g_pendingFrames == 0) {
                g_status = "Map selection was not accepted by the lobby. Check the Cbuf log.";
                LOG_WARN("Maps", "native ui_mapname did not retain '%s'", g_pendingMap.c_str());
            }
        }
        if (g_browser)
            DrawBrowser();
        else
            Function<void (*)(int)>(replay::DrawConsole)(localClient);
    } catch (...) {
        LOG_ERR("UI", "developer UI callback failed; native overlay returned normally");
    }
    return result;
}
void HandleKeys(int client, int key, bool down, unsigned time, int virtualKey, int controller) {
    auto original = g_keyEvent.load(std::memory_order_acquire);
    if (!g_ready.load(std::memory_order_acquire) || client != 0 || !MainThread() || key < 0 ||
        key >= 256) {
        original(client, key, down, time, virtualKey, controller);
        return;
    }
    if (key == 140 || key == 141)
        g_shift[key - 140] = down;
    if (!down) {
        if (g_consumed[key]) {
            g_consumed[key] = false;
            return;
        }
        original(client, key, down, time, virtualKey, controller);
        return;
    }
    // The stock handler can return through its UI/input consumers before its
    // console-toggle branch. Handle the physical key here on the main thread.
    // VK_OEM_3 covers layouts that map the grave key to a different game code.
    if (key == 96 || virtualKey == VK_OEM_3 || key == 0xA0) // grave or K_F7
    {
        if (!g_consumed[key]) {
            if (g_browser)
                SetBrowser(false);
            else if (g_shift[0] || g_shift[1]) {
                if (!ConsoleActive())
                    ToggleConsole();
                Function<void (*)()>(replay::ToggleOutput)();
            } else
                ToggleConsole();
            LOG_INFO("UI", "console key=%d virtual=%d active=%d", key, virtualKey,
                     int(ConsoleActive()));
        }
        g_consumed[key] = true;
        return;
    }
    if (key == 0x9F) // Replay K_F6
    {
        if (!g_consumed[key])
            SetBrowser(!g_browser);
        g_consumed[key] = true;
        return;
    }
    if (g_browser) {
        const bool repeated = g_consumed[key];
        g_consumed[key] = true;
        if (key == 27 || key == 96)
            SetBrowser(false);
        else if (key == 132 && g_selection)
            --g_selection;
        else if (key == 133 && g_selection + 1 < g_packages.size())
            ++g_selection;
        else if (!repeated && key == 13)
            SelectPackage();
        else if (!repeated && key == 'r')
            Refresh();
        else if (!repeated && key == 8)
            RestoreStock();
        return;
    }
    original(client, key, down, time, virtualKey, controller);
}
void __fastcall
Keys(int client, int key, bool down, unsigned time, int virtualKey, int controller) {
    try {
        HandleKeys(client, key, down, time, virtualKey, controller);
    } catch (const std::exception& error) {
        LOG_ERR("UI", "package browser input failed: %s", error.what());
        // Keep a filesystem/metadata error from crossing into game exception handling.
        if (g_browser && MainThread())
            SetBrowser(false);
    }
}
void __fastcall ConsoleText(int client, const char* text) {
    auto original = g_addText.load(std::memory_order_acquire);
    if (!g_ready.load(std::memory_order_acquire) || client != 0) {
        original(client, text);
        return;
    }
    char input[4096]{};
    const auto length = safemem::ReadString(text, input, sizeof(input));
    if (!length || length == sizeof(input) - 1) {
        original(client, text);
        return;
    }
    std::string translated;
    try {
        std::string command(input);
        const auto end = command.find_last_not_of(" \r\n\t");
        if (end != std::string::npos && command.substr(0, end + 1) == "mw_maps") {
            g_openRequested.store(true);
            return;
        }
        translated = commandtext::Translate(command);
        if (translated != command)
            LOG_INFO("Cbuf", "translated console dvar names: %s", translated.c_str());
    } catch (...) {
        original(client, text);
        return;
    }
    if (MainThread())
        Submit(translated);
    else
        original(client, translated.c_str());
}
bool Matches(const replay::Binding& binding) {
    uint8_t bytes[64]{};
    return safemem::ReadBytes(reinterpret_cast<void*>(g_base + binding.rva), bytes, binding.size) &&
           memcmp(bytes, binding.bytes, binding.size) == 0;
}
}

namespace developerui {
hook::Status Install(uintptr_t base) {
    if (g_ready.load(std::memory_order_acquire))
        return hook::Status::Installed;
    g_base = base;
    for (const auto* binding :
         {&replay::DrawText, &replay::DrawPic, &replay::FindAsset, &replay::DrawConsole,
          &replay::ToggleConsole, &replay::ToggleOutput, &replay::ConsoleActive,
          &replay::IsMainThread, &replay::IsFrontEnd, &replay::GetString, &replay::GetBool,
          &replay::EnterCritical, &replay::LeaveCritical})
        if (!Matches(*binding))
            return hook::Status::NotReady;
    auto status = hook::Install(reinterpret_cast<void*>(base + replay::Overlay.rva), &DrawOverlay,
                                replay::Overlay.bytes, replay::Overlay.size, g_overlay);
    if (status != hook::Status::Installed)
        return status;
    status = hook::Install(reinterpret_cast<void*>(base + replay::KeyEvent.rva), &Keys,
                           replay::KeyEvent.bytes, replay::KeyEvent.size, g_keyEvent);
    if (status != hook::Status::Installed)
        return status;
    status = hook::Install(reinterpret_cast<void*>(base + replay::AddConsoleText.rva), &ConsoleText,
                           replay::AddConsoleText.bytes, replay::AddConsoleText.size, g_addText);
    if (status == hook::Status::Installed) {
        g_ready.store(true, std::memory_order_release);
        LOG_INFO(
            "UI",
            "Replay native console, dvar-name translation and F6 custom-map browser installed");
    }
    return status;
}
}
