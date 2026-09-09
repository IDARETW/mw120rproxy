-- XMake build for the MW2019 1.20 Replay XInput proxy.
-- Run from this directory:
--   xmake f -m release -a x64 -y && xmake

set_project("mw120rproxy")
set_xmakever("2.8.0")

set_allowedplats("windows")
set_allowedarchs("x64")
set_allowedmodes("debug", "release")
set_defaultmode("release")
set_languages("cxx20", "c17")

local MODE_DEBUG = is_mode("debug")
local CFG = MODE_DEBUG and "Debug" or "Release"

target("mw120rproxy")
    set_kind("shared")
    set_basename("XInput9_1_0")
    set_prefixname("")

    -- Keep XMake products apart from the existing Visual Studio x64 directory.
    set_targetdir(path.join(os.projectdir(), "xmake-out", "x64", CFG))
    set_objectdir(path.join(os.projectdir(), ".xmake", "obj", CFG))

    add_defines("WIN32", "_WINDOWS", "_USRDLL", "NOMINMAX", "WIN32_LEAN_AND_MEAN",
                "_CRT_SECURE_NO_WARNINGS")
    add_includedirs("sdk_shim")
    add_syslinks("kernel32", "user32", "advapi32", "ntdll", "bcrypt")
    add_cxflags("/permissive-", "/sdl", "/W3", {tools = "cl"})
    add_shflags("/DEF:XInput9_1_0.def", "/SUBSYSTEM:WINDOWS", {force = true})

    if MODE_DEBUG then
        set_runtimes("MTd")
        add_defines("_DEBUG")
        set_symbols("debug")
    else
        set_runtimes("MT")
        add_defines("NDEBUG")
        add_cxflags("/Gy", "/Oi", {tools = "cl"})
        add_shflags("/OPT:REF", "/OPT:ICF", "/Brepro", "/emittoolversioninfo:no", {force = true})
        set_symbols("debug")
    end

    add_files(
        "dllmain.cpp",
        "diagnostics.cpp",
        "engine_diagnostics.cpp",
        "startup_compat.cpp",
        "offline_auth.cpp",
        "fastfile_diagnostics.cpp",
        "developer_ui.cpp",
        "command_text.cpp",
        "noclip.cpp",
        "custom_map_loader.cpp",
        "custom_physics.cpp",
        "custom_collision.cpp",
        "custom_ladders.cpp",
        "custom_glass.cpp",
        "custom_doors.cpp",
        "custom_door_ui.cpp",
        "custom_map_ui.cpp",
        "custom_images.cpp",
        "custom_audio.cpp",
        "custom_surfaces.cpp",
        "custom_render.cpp",
        "custom_omnvars.cpp",
        "custom_maps.cpp",
        "file_open_hook.cpp",
        "dvar_patches.cpp",
        "inline_hook.cpp",
        "log.cpp",
        "proxy.cpp",
        "trigger.cpp",
        "utils.cpp")
    add_files("thirdparty/minhook/src/*.c", "thirdparty/minhook/src/hde/hde64.c")
