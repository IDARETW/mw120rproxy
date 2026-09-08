#pragma once

#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

namespace custommaps
{
    struct Package
    {
        std::string id;
        std::string title;
        std::string description;
        // Optional installed Replay shader dependency; no stock world is loaded.
        std::string shaderSource;
        std::wstring directory;
        std::string error;
        bool valid = false;
    };

    // Scan <game>\mods\mw120r\maps. This function does not create or alter files.
    void Initialize(HMODULE self);
    void Refresh();

    // The returned package state is a snapshot. It is safe for a menu or a hook to keep it.
    std::vector<Package> List();
    bool Select(const char* id);
    void ClearSelection();
    std::string Active();
    std::string Status(const char* id);

    // Return a file only for the selected valid package and its five expected zones.
    bool ActiveZonePath(const char* zoneName, std::wstring& pathOut);
    bool ActiveShaderSource(const char* zoneName, std::string& sourceOut);
    // Build a game-relative path only for a requested selected zone.
    bool ActiveZoneQPath(const char* request, char* qpathOut, size_t qpathOutSize);
    // Match only canonical stock zone request locations for the selected family.
    bool ResolveDiskRead(const char* request, std::string& pathOut);
    bool IsKnownMap(const char* mapName);
}
