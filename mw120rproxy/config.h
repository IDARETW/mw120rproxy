#pragma once
#include <windows.h>
#include <string>

// Optional settings next to the proxy. The default profile enables offline MP.
namespace config
{
    struct Settings
    {
        bool  console      = true;    // attach/create the shared console
        bool  hookBool     = true;    // override the offline bool-dvar set (Dvar_RegisterBool)
        bool  hookVariant  = false;   // trace every dvar (Dvar_RegisterVariant) -> mw120rproxy.trace.log
        bool  offlineAuth  = true;    // consistent local controller identity and offline auth state
        bool  fastfileDiagnostics = true; // observe native DB open results without redirecting them
        bool  engineDiagnostics = true; // native debug output and error call chains
        bool  developerUI = true; // native in-game console and F6 package browser
        bool  customMapLoader = true; // selected-package reads through native disk handles
        bool  customMapFileMonitor = false; // trace selected zone file-open requests
        bool  customMapFileRedirect = false; // replace an observed selected zone path
        std::string hookTrigger = "loadimage"; // "loadimage" (default) | "prologue"
    };

    inline Settings Load(HMODULE self)
    {
        Settings s;
        wchar_t path[MAX_PATH]{};
        DWORD len = GetModuleFileNameW(self, path, MAX_PATH);
        if (!len || len >= MAX_PATH) return s;
        wchar_t* slash = wcsrchr(path, L'\\');
        if (!slash) return s;
        slash[1] = 0;
        wchar_t ini[MAX_PATH];
        if (_snwprintf_s(ini, _countof(ini), _TRUNCATE, L"%smw120rproxy.ini", path) <= 0) return s;

        auto geti = [&](const wchar_t* key, int def) {
            return GetPrivateProfileIntW(L"mw120rproxy", key, def, ini);
        };
        s.console     = geti(L"console", s.console) != 0;
        s.hookBool    = geti(L"hook_bool", s.hookBool) != 0;
        s.hookVariant = geti(L"hook_variant", s.hookVariant) != 0;
        s.offlineAuth = geti(L"offline_auth", s.offlineAuth) != 0;
        s.fastfileDiagnostics = geti(L"fastfile_diagnostics", s.fastfileDiagnostics) != 0;
        s.engineDiagnostics = geti(L"engine_diagnostics", s.engineDiagnostics) != 0;
        s.developerUI = geti(L"developer_ui", s.developerUI) != 0;
        s.customMapLoader = geti(L"custom_map_loader", s.customMapLoader) != 0;
        s.customMapFileMonitor = geti(L"custom_map_file_monitor", s.customMapFileMonitor) != 0;
        s.customMapFileRedirect = geti(L"custom_map_file_redirect", s.customMapFileRedirect) != 0;
        if (s.customMapFileRedirect) s.customMapFileMonitor = true;

        wchar_t trig[64]{};
        GetPrivateProfileStringW(L"mw120rproxy", L"hook_trigger", L"loadimage", trig, _countof(trig), ini);
        char trigA[64]{};
        WideCharToMultiByte(CP_ACP, 0, trig, -1, trigA, sizeof(trigA), nullptr, nullptr);
        s.hookTrigger = trigA;

        return s;
    }
}
